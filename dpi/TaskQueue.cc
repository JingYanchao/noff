//
// Created by root on 17-5-16.
//

#include "TaskQueue.h"

#include <muduo/base/Exception.h>

#include <assert.h>
#include <stdio.h>

TaskQueue::TaskQueue(const muduo::string& nameArg)
        : name_(nameArg)
{
}

TaskQueue::~TaskQueue()
{
    stop();
}

void TaskQueue::start()
{
    thread_.reset(new muduo::Thread(
            std::bind(&TaskQueue::runInThread, this), name_));
    thread_->start();
}

void TaskQueue::stop()
{
    Task emptyTask;
    while (!run(emptyTask))
        ;
    thread_->join();
}

size_t TaskQueue::queueSize() const
{
    return static_cast<size_t >(queue_.size());
}

bool TaskQueue::run(const Task& task)
{
    return queue_.try_push(task);
}

bool TaskQueue::nonBlockingRun(Task &&task)
{
    return queue_.try_push(std::move(task));
}

TaskQueue::Task TaskQueue::take()
{
    Task t;
    queue_.pop(t);
    return t;
}

void TaskQueue::runInThread()
{
    try
    {
        if (threadInitCallback_)
        {
            threadInitCallback_();
        }
        while (true)
        {
            Task task(take());
            if (task) {
                task();
            }
            else {
                break;
            }
        }
    }
    catch (const muduo::Exception& ex)
    {
        fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
        fprintf(stderr, "reason: %s\n", ex.what());
        fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
        abort();
    }
    catch (const std::exception& ex)
    {
        fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
        fprintf(stderr, "reason: %s\n", ex.what());
        abort();
    }
    catch (...)
    {
        fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
        throw; // rethrow
    }
}

