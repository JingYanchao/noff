//
// Created by root on 17-4-13.
//

#include "Capture.h"
#include "Dispatcher.h"

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>

using std::placeholders::_1;
using std::placeholders::_2;

class IpFragmentCounter {

public:
    // Dispatcher callbacks must be thread safe !!!
    void onIpFragment(ip *hdr, int len)
    {
        counter_.add(1);
        // do NOT manually free(hdr),
        // Dispatcher will do it for us
    }

    ~IpFragmentCounter() {
        LOG_TRACE << counter_.get() << " packet received";
    }

private:
    muduo::AtomicInt32 counter_;
};

Capture cap("any", 65536, true, 1000);

void sigHandler(int)
{
    cap.breakLoop();
}


int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::TRACE);

    signal(SIGINT, sigHandler);

    cap.setFilter("ip");

    // customized function, count IP fragments
    IpFragmentCounter counter;

    // connect Dispatcher and IpFragmentCounter
    size_t nWorkers = 4;
    std::vector<Dispatcher::IpFragmentCallback> callbacks(
            nWorkers, std::bind(
                    &IpFragmentCounter::onIpFragment, &counter, _1, _2));


    // if queue is full, Dispatcher will warn
    // Dispatcher dis(callbacks, 2);
    Dispatcher dispatcher(callbacks, 1024);

    // connect Capture and Dispatcher
    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dispatcher, _1, _2));

    cap.startLoop(0);
}