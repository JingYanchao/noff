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
#include <net/ethernet.h>
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

template <typename T>
class Counter : muduo::noncopyable
{
public:
    typedef std::function<void(const T&)> CounterCallback;

    void onData(T *data, timeval timeStamp)
    {
        onData(*data, timeStamp);
    }

    virtual void onData(T &data, timeval timeStamp)
    {

    }

    void setCounterCallback(const CounterCallback& cb)
    {
        counterCallback_ = cb;
    }

protected:
    void processExpire(timeval timeStamp)
    {
        bool expire = false;
        {
            muduo::MutexLockGuard guard(lock);
            if (timeStamp.tv_sec >= sendTimestamp) {
                sendTimestamp = timeStamp.tv_sec + interval_;
                expire = true;
            }
        }

        if (expire && counterCallback_) {
            counterCallback_(
                    { counter[0].getAndSet(0), counter[1].getAndSet(0),
                      counter[2].getAndSet(0), counter[3].getAndSet(0),
                      counter[4].getAndSet(0), counter[5].getAndSet(0),
                      counter[6].getAndSet(0) });
        }
    }

private:

    int interval_;

    CounterCallback counterCallback_;

    muduo::MutexLock lock;
    int64_t sendTimestamp;
};

#endif //NOFF_TESTCOUNTER_H
