//
// Created by root on 17-5-16.
//

#ifndef NOFF_TASKQUEUE_H
#define NOFF_TASKQUEUE_H

#include <functional>
#include <string>
#include <vector>

#include <muduo/base/noncopyable.h>
#include <muduo/base/Thread.h>
#include <tbb/concurrent_queue.h>

class TaskQueue : muduo::noncopyable
{
public:
    typedef std::function<void ()> Task;

    explicit TaskQueue(const muduo::string& name);
    ~TaskQueue();

    // Must be called before start().
    void setMaxQueueSize(int maxSize)
    {
        maxSize_ = maxSize;
    }

    void setThreadInitCallback(const Task& cb)
    {
        threadInitCallback_ = cb;
    }

    void start();
    void stop();

    const muduo::string& name() const
    { return name_; }

    size_t queueSize() const;

    // Could block if maxQueueSize > 0
    bool nonBlockingRun(const Task &f);
    bool nonBlockingRun(Task &&f);

private:
    void runInThread();
    Task take();

    int maxSize_;
    muduo::string name_;
    Task threadInitCallback_;
    std::unique_ptr<muduo::Thread> thread_;
    tbb::concurrent_bounded_queue<Task> queue_;
};


#endif //NOFF_TASKQUEUE_H
