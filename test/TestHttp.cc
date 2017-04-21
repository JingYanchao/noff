//
// Created by root on 17-4-21.
//

#include <unistd.h>
#include <string.h>
#include <vector>
#include <string>
#include <sstream>

#include <muduo/base/Logging.h>

#include "Capture.h"
#include "Dispatcher.h"
#include "IpFragment.h"
#include "TcpFragment.h"
#include "Http.h"

using namespace std;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;
using std::placeholders::_5;

void onHttpRequest(HttpRequest *req)
{
    string          str;
    ostringstream   is(str);

    is << "HTTP request\n"
       << "method" << req->method << "\n";

    req << ""req->method
}

int main(int argc, char **argv)
{
    int     opt;
    char    deviceName[32];
    int     nPackets, nWorkers;

    while ( (opt = getopt(argc, argv, "i:c:t:"))) {
        switch (opt) {
            case 'i':
                if (strlen(optarg) >= 31) {
                    LOG_ERROR << "device name too long";
                }
                strcpy(deviceName, optarg);
                break;
            case 'c':
                nPackets = atoi(optarg);
                break;
            case 't':
                nWorkers = atoi(optarg);
                break;
            default:
                LOG_ERROR << "usage: [-i interface] [-c count] [-t threads]";
                exit(1);
        }
    }

    Capture                  cap(deviceName, 65560, true, 1000);
    std::vector<IpFragment>  ip(nWorkers);
    std::vector<TcpFragment> tcp(nWorkers);
    std::vector<Http>        http(nWorkers);

    std::vector<Dispatcher::IpFragmentCallback> callbacks;

    for (int i = 0; i < nWorkers; ++i) {

        callbacks.push_back(bind(
                &IpFragment::startIpfragProc, &ip[i], _1, _2));

        ip[i].addTcpCallback(bind(
                &TcpFragment::processTcp, &tcp[i], _1, _2));

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
    }

    Dispatcher disp(callbacks, 65536);

    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &disp, _1 ,_2));
}