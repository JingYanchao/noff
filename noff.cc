//
// Created by root on 17-4-21.
//

#include <unistd.h>
#include <string.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <arpa/inet.h>
#include <signal.h>
#include <memory>


#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/base/Atomic.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include <muduo/base/Singleton.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/ThreadPool.h>
#include <dpi/Udp.h>


#include "Capture.h"
#include "PFCapture.h"
#include "IpFragment.h"
#include "TcpFragment.h"
#include "Dispatcher.h"
#include "Dispatcher2.h"
#include "mac/MacCount.h"
#include "protocol/ProtocolPacketCounter.h"
#include "http/Http.h"
#include "dns/Dnsparser.h"
#include "header/TcpHeader.h"
#include "UdpClient.h"
#include "header/TcpHeader.h"

using namespace std;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;
using std::placeholders::_5;

vector<unique_ptr<PFCapture>>
                        caps;
unique_ptr<UdpClient>   httpRequestOutput = NULL;
unique_ptr<UdpClient>   httpResponseOutput = NULL;
unique_ptr<UdpClient>   dnsRequestOutput = NULL;
unique_ptr<UdpClient>   dnsResponseOutput = NULL;
unique_ptr<UdpClient>   tcpHeaderOutput = NULL;
unique_ptr<UdpClient>   macCounterOutput = NULL;
unique_ptr<UdpClient>   packetCounterOutput = NULL;

bool running = true;

void sigHandler(int)
{
    assert(!caps.empty());
    for (auto &c : caps) {
        c->breakLoop();
    }
    if (running) {
        running = false;
    }
    exit(0);
}

#define globalInstance(Type) \
muduo::Singleton<Type>::instance()

#define threadInstance(Type) \
muduo::ThreadLocalSingleton<Type>::instance()

void setHttpInThread()
{
    assert(httpRequestOutput != NULL);
    assert(httpResponseOutput != NULL);

    auto& tcp = threadInstance(TcpFragment);
    auto& http = threadInstance(Http);

    // tcp connection->http
    tcp.addConnectionCallback(bind(
            &Http::onTcpConnection, &http, _1, _2));

    // tcp data->http
    tcp.addDataCallback(bind(
            &Http::onTcpData, &http, _1, _2, _3, _4, _5));

    // tcp close->http
    tcp.addTcpcloseCallback(bind(
            &Http::onTcpClose, &http, _1, _2));

    // tcp rst->http
    tcp.addRstCallback(bind(
            &Http::onTcpRst, &http, _1, _2));

    // tcp timeout->http
    tcp.addTcptimeoutCallback(bind(
            &Http::onTcpTimeout, &http, _1, _2));

    // http request->udp client
    http.addHttpRequestCallback(bind(
            &UdpClient::onDataPointer<HttpRequest>, httpRequestOutput.get(), _1));

    // http response->udp client
    http.addHttpResponseCallback(bind(
            &UdpClient::onDataPointer<HttpResponse>, httpResponseOutput.get(), _1));
}

void setPacketCounterInThread()
{
    assert(packetCounterOutput != NULL);

    auto& udp = threadInstance(Udp);
    auto& tcp = threadInstance(TcpFragment);
    auto& counter = globalInstance(ProtocolPacketCounter);

    // tcp->packet counter
     tcp.addDataCallback(bind(
            &ProtocolPacketCounter::onTcpData, &counter, _1, _2));

    // udp->packet counter
    udp.addUdpCallback(bind(
            &ProtocolPacketCounter::onUdpData, &counter, _1, _2, _3, _4));
    // packet->udp output
    counter.setCounterCallback(bind(
            &UdpClient::onData<CounterDetail>, packetCounterOutput.get(), _1));
}

void setDnsCounterInThread()
{
    assert(dnsRequestOutput != NULL);
    assert(dnsResponseOutput != NULL);

    auto& udp = threadInstance(Udp);
    auto& dns = threadInstance(DnsParser);

    udp.addUdpCallback(bind(
            &DnsParser::processDns, &dns, _1, _2, _3, _4));

    dns.addRequstecallback(bind(
            &UdpClient::onData<DnsRequest>,
            dnsRequestOutput.get(), _1));

    dns.addResponsecallback(bind(
            &UdpClient::onData<DnsResponse>,
            dnsResponseOutput.get(), _1));
}

//void setMacCounterInThread()
//{
//    assert(cap != NULL);
//    assert(macCounterOutput != NULL);
//
//    auto& mac = globalInstance(MacCount);
//
//    cap->addPacketCallBack(bind(
//            &MacCount::processMac, &mac, _1, _2, _3));
//
//    mac.addEtherCallback(bind(
//            &UdpClient::onData<MacInfo>, macCounterOutput.get(), _1));
//}

void setTcpHeaderInThread()
{
    assert(tcpHeaderOutput != NULL);

    auto& ip = threadInstance(IpFragment);
    auto& header = threadInstance(TcpHeader);

    ip.addTcpCallback(bind(
            &TcpHeader::processTcpHeader, &header, _1, _2, _3));

    header.addTcpHeaderCallback(bind(
            &UdpClient::onData<tcpheader>, tcpHeaderOutput.get(), _1));
}

void initInThread()
{
    // assert(!caps.empty());

    auto& ip = threadInstance(IpFragment);
    auto& udp = threadInstance(Udp);
    auto& tcp = threadInstance(TcpFragment);

    // ip->tcp
    ip.addTcpCallback(bind(
            &TcpFragment::processTcp, &tcp, _1, _2, _3));

    // ip->udp
    ip.addUdpCallback(bind(
            &Udp::processUdp, &udp, _1, _2, _3));

    // tcp->http->udp output
    setHttpInThread();

    // tcp->packet counter->udp output
    setPacketCounterInThread();

    // udp->dns>udp
    setDnsCounterInThread();

    // tcp->udp
    setTcpHeaderInThread();
}

int main(int argc, char **argv)
{
    int opt;
    char name[32] = "any";
    int nPackets = 0;
    int nWorkers = 1;
    int taskQueSize = 65536;
    bool singleThread = false;
    char ipAddress[16] = "10.255.0.12";
    uint16_t port = 10666;
    uint16_t port2 = 30001;

    muduo::Logger::setLogLevel(muduo::Logger::INFO);

    while ((opt = getopt(argc, argv, "i:c:t:h:p:")) != -1) {
        switch (opt) {
            case 'i':
                if (strlen(optarg) >= 31) {
                    LOG_ERROR << "device name too long";
                    exit(1);
                }
                strcpy(name, optarg);
                break;
            case 'c':
                nPackets = atoi(optarg);
                break;
            case 't':
                nWorkers = atoi(optarg);
                if (nWorkers <= 0) {
                    nWorkers = 1;
                    singleThread = true;
                }
                break;
            case 'h':
                if (strlen(optarg) >= 16) {
                    LOG_ERROR << "IP address too long";
                    exit(1);
                }
                strcpy(ipAddress, optarg);
                break;
            case 'p':
                port = (uint16_t) atoi(optarg);
                break;
            default:
                LOG_ERROR << "usage: [-i interface] [-c packet count] [-t threads] [-h ip] [-p port]";
                exit(1);
        }
    }

    //define the udp client
    httpRequestOutput.reset(new UdpClient({ipAddress, port++}, "http request"));
    httpResponseOutput.reset(new UdpClient({ipAddress, port++}, "http response"));
    dnsRequestOutput.reset(new UdpClient({ipAddress, port++}, "dns request"));
    dnsResponseOutput.reset(new UdpClient({ipAddress, port++}, "dns response"));

    tcpHeaderOutput.reset(new UdpClient({ipAddress, port2++}, "tcp header"));
    macCounterOutput.reset(new UdpClient({ipAddress, port2++}, "mac counter"));
    packetCounterOutput.reset(new UdpClient({ipAddress, port2++}, "packet counter"));

    signal(SIGINT, sigHandler);

    bool isLoopback = (strcmp("any", name) == 0);

    if (!singleThread) {

        muduo::ThreadPool pool;
        pool.setThreadInitCallback(&initInThread);
        pool.start(nWorkers);
        for (int i = 0; i < nWorkers; ++i) {
            std::string queName = std::string(name) + "@" + std::to_string(i);
            caps.emplace_back(new PFCapture(queName, 65560, isLoopback));
            pool.run([=, &caps]() {
                auto &ip = threadInstance(IpFragment);
                caps[i]->addIpFragmentCallback(std::bind(
                        &IpFragment::startIpfragProc, &ip, _1, _2, _3));
                caps[i]->setFilter("ip");
                caps[i]->startLoop(0);
            });
        }

        LOG_INFO << "all workers started";
        while (running) {
            pause();
        }
    }
    else {

        string queName = std::string(name);

        caps.emplace_back(new PFCapture(queName, 65560, isLoopback));

        initInThread();

        auto &ip = threadInstance(IpFragment);

        caps[0]->addIpFragmentCallback(std::bind(
                &IpFragment::startIpfragProc, &ip, _1, _2, _3));
        caps[0]->setFilter("ip");
        caps[0]->startLoop(0);
    }
}
