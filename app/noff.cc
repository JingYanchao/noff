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

#include "rapidjson/document.h"

using namespace std;

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;
using std::placeholders::_5;

Capture *cap;

struct NoffParam
{
    // capture
    const char  *interface = "lo";

    // output
    const char *output = "stdout";

    // dispacher
    bool multithread = true;
    int workers = 4;
    int queue = 65536;

};

#define CHECK_JSON(exp, str) \
do  { \
    if (!exp) { \
        LOG_ERROR << "Parse: " << str; \
        exit(1); \
    } \
} while(0)

NoffParam parseParam(const char *json)
{
    NoffParam param;

    rapidjson::Document doc;
    doc.Parse(json);

    param.interface = doc["interface"].GetString();
    param.output = doc["output"].GetString();
    param.workers = doc["workers"].GetInt();
    param.queue = doc["queue"].GetInt();
    param.multithread = doc["multithread"].GetBool();

    if (!param.multithread) {
        param.workers = 1;
    }

    return param;
}

void sigHandler(int)
{
    assert(cap != NULL);
    cap->breakLoop();
}

int main(int argc, char **argv)
{
    muduo::Logger::setLogLevel(muduo::Logger::INFO);

    const char *fileName;
    FILE *file;
    char json[102400];

    if (argc != 2) {
        LOG_ERROR << "usage: noff filename";
        exit(1);
    }

    if (strlen(argv[1]) >= 31) {
        LOG_ERROR << "file name too long";
        exit(1);
    }

    fileName = argv[1];

    file = fopen(fileName, "r");
    if (file == NULL) {
        LOG_ERROR << "file not exists";
        exit(1);
    }

    size_t len = fread(json, 1, sizeof(json), file);
    json[len] = '\0';

    NoffParam param = parseParam(json);

    std::vector<IpFragment>  ip(param.workers);
    std::vector<TcpFragment> tcp(param.workers);
    std::vector<Http>        http(param.workers);

    std::vector<Dispatcher::IpFragmentCallback> callbacks;

    for (int i = 0; i < param.workers; ++i) {

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


        // http[i].addHttpRequestCallback(onHttpRequest);
        // http[i].addHttpResponseCallback(onHttpResponse);
    }

    cap = new Capture(param.interface, 70000, true, 1000);
    cap->setFilter("ip");


    signal(SIGINT, sigHandler);

    if (!param.multithread) {
        for (auto& cb : callbacks) {
            cap->addIpFragmentCallback(cb);
        }
        cap->startLoop(0);
    }
    else {
        Dispatcher disp(callbacks, param.queue);
        cap->addIpFragmentCallback(std::bind(
                &Dispatcher::onIpFragment, &disp, _1, _2, _3));
        cap->startLoop(0);
    }
}