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
#include <dpi/Udp.h>


#include "Capture.h"
#include "Dispatcher.h"
#include "IpFragment.h"
#include "TcpFragment.h"
#include "MacCount.h"
#include "ProtocolPacketCounter.h"
#include "Http.h"
#include "Dnsparser.h"
#include "UdpClient.h"
#include "header/TcpHeader.h"

using namespace std;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;
using std::placeholders::_5;

unique_ptr<Capture>     cap = NULL;
unique_ptr<muduo::CountDownLatch>
                        countDown = NULL;
unique_ptr<UdpClient>   httpRequestOutput = NULL;
unique_ptr<UdpClient>   httpResponseOutput = NULL;
unique_ptr<UdpClient>   dnsRequestOutput = NULL;
unique_ptr<UdpClient>   dnsResponseOutput = NULL;
unique_ptr<UdpClient>   packetCounterOutput = NULL;
unique_ptr<UdpClient>   macCounterOutput = NULL;
unique_ptr<UdpClient>   tcpHeaderOutput = NULL;


void sigHandler(int)
{
    assert(cap != NULL);
    cap->breakLoop();
}

#define threadInstance(Type) \
muduo::ThreadLocalSingleton<Type>::instance()

#define globalInstance(Type) \
muduo::Singleton<Type>::instance()

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

void setMacCounterInThread()
{
    assert(cap != NULL);
    assert(macCounterOutput != NULL);

    auto& mac = globalInstance(MacCount);

    cap->addPacketCallBack(bind(
            &MacCount::processMac, &mac, _1, _2, _3));

    mac.addEtherCallback(bind(
            &UdpClient::onData<MacInfo>, macCounterOutput.get(), _1));
}

void initInThread()
{
    assert(cap != NULL);

    auto& ip = threadInstance(IpFragment);
    auto& udp = threadInstance(Udp);
    auto& tcp = threadInstance(TcpFragment);

    // ip->tcp
    ip.addTcpCallback(bind(
            &TcpFragment::processTcp, &tcp, _1, _2, _3));

    // ip->udp
    ip.addUdpCallback(bind(
            &Udp::processUdp, &udp, _1, _2, _3));

    //ip->tcphdr
    tcpHdr.addTcpHeaderCallback(bind(
            &UdpClient::onData<tcpheader>, tcpHeaderOutput.get(), _1));

    // tcp->http->udp output
    setHttpInThread();

    // tcp->packet counter->udp output
    setPacketCounterInThread();
    //udp->dns>udp
    setDnsCounterInThread();

    countDown->countDown();
}

int main(int argc, char **argv)
{
    int     opt;
    char    name[32] = "any";
    int     nPackets = 0;
    int     nWorkers = 1;
    int     threadQueSize = 65536;
    bool    fileCapture = false;
    bool    singleThread = false;
    uint16_t  port = 2333;

    muduo::Logger::setLogLevel(muduo::Logger::INFO);

    while ( (opt = getopt(argc, argv, "f:i:c:t:p:")) != -1)
    {
        switch (opt)
        {
            case 'f':
                fileCapture = true;
                /* fall through */
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
            case 'p':
                port = (uint16_t)atoi(optarg);
                break;
            default:
                LOG_ERROR << "usage: [-i interface] [-c packet count] [-t threads] [-p port]";
                exit(1);
        }
    }

    //define the udp client
    httpRequestOutput.reset(new UdpClient({"127.0.0.1", port++}, "http request"));
    httpResponseOutput.reset(new UdpClient({"127.0.0.1", port++}, "http response"));
    dnsRequestOutput.reset(new UdpClient({"127.0.0.1", port++}, "dns request"));
    dnsResponseOutput.reset(new UdpClient({"127.0.0.1", port++}, "dns response"));
    packetCounterOutput.reset(new UdpClient({"127.0.0.1", port++}, "packet counter"));
    macCounterOutput.reset(new UdpClient({"127.0.0.1", port++}, "mac counter"));
    tcpHeaderOutput.reset(new UdpClient({"127.0.0.1", port++}, "tcp header"));

    countDown.reset(new muduo::CountDownLatch(nWorkers));

    if (fileCapture) {
        cap.reset(new Capture(name));
    }
    else {
        cap.reset(new Capture(name, 70000, true, 1000));
        cap->setFilter("ip");
    }

    signal(SIGINT, sigHandler);

    // pcap->mac counter->udp
    setMacCounterInThread();

    if (singleThread)
    {
        initInThread();

        auto& ip = threadInstance(IpFragment);
        cap->addIpFragmentCallback(std::bind(
                &IpFragment::startIpfragProc, &ip, _1, _2, _3));
        countDown->wait();
        cap->startLoop(nPackets);
    }
    else
    {
        Dispatcher disp(nWorkers, threadQueSize, &initInThread);
        cap->addIpFragmentCallback(std::bind(
                &Dispatcher::onIpFragment, &disp, _1, _2, _3));

        countDown->wait();
        cap->startLoop(nPackets);
    }
}
