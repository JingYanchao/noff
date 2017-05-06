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

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/base/Atomic.h>


#include "Capture.h"
#include "Dispatcher.h"
#include "IpFragment.h"
#include "TcpFragment.h"
#include "Http.h"
#include "UdpClient.h"

using namespace std;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;
using std::placeholders::_5;

Capture *cap;

void sigHandler(int)
{
    assert(cap != NULL);
    cap->breakLoop();
}

int main(int argc, char **argv)
{
    int     opt;
    char    name[32] = "any";
    int     nPackets = 0, nWorkers = 1;
    bool    fileCapture = false;
    bool    singleThread = false;
    uint16_t  port = 9877;

    muduo::Logger::setLogLevel(muduo::Logger::INFO);

    while ( (opt = getopt(argc, argv, "f:i:c:t:p:")) != -1) {
        switch (opt) {
            case 'f':
                fileCapture = true;
                /* fall through */
            case 'i':
                if (strlen(optarg) >= 31) {
                    LOG_ERROR << "device name too long";
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

    std::vector<IpFragment>  ip(nWorkers);
    std::vector<TcpFragment> tcp(nWorkers);
    std::vector<Http>        http(nWorkers);

    UdpClient                httpClient({"127.0.0.1", port});

    std::vector<Dispatcher::IpFragmentCallback> callbacks;

    for (int i = 0; i < nWorkers; ++i) {

        callbacks.push_back(bind(
                &IpFragment::startIpfragProc, &ip[i], _1, _2, _3));

        ip[i].addTcpCallback(bind(
                &TcpFragment::processTcp, &tcp[i], _1, _2, _3));

        tcp[i].addConnectionCallback(bind(
                &Http::onTcpConnection, &http[i], _1, _2));

        tcp[i].addDataCallback(bind(
                &Http::onTcpData, &http[i], _1, _2, _3, _4, _5));

        tcp[i].addTcpcloseCallback(bind(
                &Http::onTcpClose, &http[i], _1, _2));

        tcp[i].addRstCallback(bind(
                &Http::onTcpRst, &http[i], _1, _2));

        tcp[i].addTcptimeoutCallback(bind(
                &Http::onTcpTimeout, &http[i], _1, _2));

        http[i].addHttpRequestCallback(bind(
                &UdpClient::onHttpRequest, &httpClient, _1));
        http[i].addHttpResponseCallback(bind(
                &UdpClient::onHttpResponse, &httpClient, _1));
    }

    if (fileCapture) {
        cap = new Capture(name);
    }
    else {
        cap = new Capture(name, 70000, true, 1000);
        cap->setFilter("ip");
    }

    signal(SIGINT, sigHandler);

    if (singleThread) {
        for (auto& cb : callbacks) {
            cap->addIpFragmentCallback(cb);
        }
        cap->startLoop(nPackets);
    }
    else {
        Dispatcher disp(callbacks, 65536);
        cap->addIpFragmentCallback(std::bind(
                &Dispatcher::onIpFragment, &disp, _1, _2, _3));
        cap->startLoop(nPackets);
    }
}