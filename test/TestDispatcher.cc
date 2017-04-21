//
// Created by root on 17-4-13.
//

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>

#include "../dpi/Capture.h"
#include "../dpi/Dispatcher.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

class IpFragmentCounter {

public:
    // Dispatcher callbacks must be thread safe !!!
    void onIpFragment(const ip *hdr, int len, timeval timeStamp)
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

Capture cap("enp3s0", 65536, true, 1000);

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
    size_t nWorkers = 1;
    std::vector<Dispatcher::IpFragmentCallback> callbacks(
            nWorkers, std::bind(
                    &IpFragmentCounter::onIpFragment, &counter, _1, _2, _3));


    // if queue is full, Dispatcher will warn
    // Dispatcher dis(callbacks, 2);
    Dispatcher dispatcher(callbacks, false, 1024);

    // connect Capture and Dispatcher
    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dispatcher, _1, _2, _3));

    cap.startLoop(0);
}