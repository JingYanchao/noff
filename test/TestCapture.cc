//
// Created by frabk on 17-4-10.
//

#include <functional>
#include <signal.h>
#include <muduo/base/Logging.h>

#include "../dpi/Capture.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

Capture *cap;

void sigHandler(int)
{
    cap->breakLoop();
}

class IpFragmentCounter
{

public:
    void onMessage(const ip *, int len, timeval timeStamp)
    {
        if (++counter_ % 1 == 0)
        {
            LOG_INFO << counter_ << " IP fragments received";
        }
    }

private:
    int counter_ = 0;
};

int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::TRACE);

    signal(SIGINT, sigHandler);

    for (int i = 0; i < 1; ++i) {

        IpFragmentCounter   ipCounter;

        cap = new Capture("test.pcap");

        // cap->setFilter("ip");

        cap->addIpFragmentCallback(std::bind(
                &IpFragmentCounter::onMessage, &ipCounter, _1, _2, _3));

        // block until enough packets received
        cap->startLoop(100);

        // break loop(if still running) and close capture
        delete cap;
    }
}