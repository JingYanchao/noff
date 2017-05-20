//
// Created by root on 17-5-20.
//

#ifndef NOFF_TASKQUEUE2_H
#define NOFF_TASKQUEUE2_H

#include <functional>
#include <vector>
#include <memory>
#include <netinet/ip.h>

#include <muduo/base/noncopyable.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/LogStream.h>
#include <muduo/base/Thread.h>


class TaskQueue2:muduo::noncopyable
{
public:
    typedef std::function<void(ip*, int, timeval)> IpFragmentCallback;
    typedef std::function<void ()> Task;

    TaskQueue2(const muduo::string& name, int flushInterval = 60);
    ~TaskQueue2()
    {
        if (running_) {
            stop();
        }
    }

    void setThreadInitCallback(const Task& cb)
    {
        threadInitCallback_ = cb;
    }

    const muduo::string& name() const
    { return name_; }


    void append(const ip*, int, timeval);

    void start()
    {
        running_ = true;
        thread_.start();
        latch_.wait();
    }

    void stop()
    {
        running_ = false;
        cond_.notify();
        thread_.join();
    }

private:

    typedef muduo::detail::FixedBuffer<muduo::detail::kLargeBuffer> Buffer;
    typedef std::vector<std::unique_ptr<Buffer>> BufferVector;
    typedef BufferVector::value_type BufferPtr;

    void threadFunc();
    void appendHelper(const ip*, int, timeval);
    void consumBuffer(const BufferPtr&);

    muduo::string name_;
    int flushInterval_;

    bool running_;

    Task threadInitCallback_;

    muduo::Thread thread_;
    muduo::CountDownLatch latch_;
    muduo::MutexLock mutex_;
    muduo::Condition cond_;
    BufferPtr currentBuffer_;
    BufferPtr nextBuffer_;
    BufferVector buffers_;
};


#endif //NOFF_TASKQUEUE2_H
