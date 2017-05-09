//
// Created by root on 17-5-8.
//

#ifndef NOFF_TESTCOUNTER_H
#define NOFF_TESTCOUNTER_H

#include <functional>
#include <array>

#include <muduo/base/noncopyable.h>
#include <muduo/base/Atomic.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Singleton.h>

#include "dpi/TcpFragment.h"
#include "UdpClient.h"

template <uint16_t PORT>
class TestCounter:muduo::noncopyable
{
public:

    TestCounter(const char *name) : client({"127.0.0.1", PORT}, name)
    {
    }

    void count(timeval timeStamp, const char *name)
    {
        counter.add(1);

        bool expire = false;
        {
            muduo::MutexLockGuard guard(lock);
            if (timeStamp.tv_sec >= sendTimestamp) {
                sendTimestamp = timeStamp.tv_sec + 1;
                expire = true;
            }
        }

        if (expire) {
            int x = counter.getAndSet(0);
            client.onString(name + (": " + std::to_string(x)) + "\n");
        }
    }

private:
    muduo::AtomicInt32 counter;

    muduo::MutexLock lock;
    int64_t sendTimestamp;

    UdpClient client;
};

template <int PORT>
void SimpleCounter(timeval timeStamp, const char *name)
{
    static TestCounter<PORT> counter(name);
    counter.count(timeStamp, name);
}

#endif //NOFF_TESTCOUNTER_H
