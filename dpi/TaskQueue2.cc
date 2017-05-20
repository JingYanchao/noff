//
// Created by root on 17-5-20.
//

#include <muduo/base/Logging.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include "TaskQueue2.h"
#include "IpFragment.h"

TaskQueue2::TaskQueue2(const muduo::string& name, int flushInterval)
        : name_(name),
          flushInterval_(flushInterval),
          running_(false),
          thread_(std::bind(&TaskQueue2::threadFunc, this), name),
          latch_(1),
          mutex_(),
          cond_(mutex_),
          currentBuffer_(new Buffer),
          nextBuffer_(new Buffer),
          buffers_()
{
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffers_.reserve(16);
}

void TaskQueue2::append(const ip *data, int len, timeval timeStamp)
{
    int totalLen = sizeof(int) + sizeof(timeval) + len;

    muduo::MutexLockGuard lock(mutex_);
    if (currentBuffer_->avail() > totalLen) {
        appendHelper(data, len, timeStamp);
    }
    else {
        buffers_.push_back(std::move(currentBuffer_));

        if (nextBuffer_ != NULL) {
            currentBuffer_ = std::move(nextBuffer_);
        }
        else {
            currentBuffer_.reset(new Buffer);
            LOG_WARN << "new buffer";
        }
        appendHelper(data, len, timeStamp);
        cond_.notify();
    }
}

void TaskQueue2::threadFunc()
{
    assert(running_);

    latch_.countDown();

    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->bzero();
    newBuffer2->bzero();

    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    if (threadInitCallback_)
    {
        threadInitCallback_();
    }

    while (running_) {

        assert(newBuffer1 && newBuffer1->length() == 0);
        assert(newBuffer2 && newBuffer2->length() == 0);
        assert(buffersToWrite.empty());
        {
            muduo::MutexLockGuard lock(mutex_);
            if (buffers_.empty()) {
                cond_.wait();
            }
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_ = std::move(newBuffer1);
            buffersToWrite.swap(buffers_);
            if (nextBuffer_ == NULL) {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        if (buffersToWrite.size() > 25) {
            LOG_WARN << "worker overload";
            buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());
        }

        for (auto &buffer : buffersToWrite) {
            consumBuffer(buffer);
        }

        if (buffersToWrite.size() > 2)
        {
            // drop non-bzero-ed buffers, avoid trashing
            buffersToWrite.resize(2);
        }

        if (!newBuffer1)
        {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }

        if (!newBuffer2)
        {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }

        buffersToWrite.clear();
    }
}

void TaskQueue2::appendHelper(const ip *data, int len, timeval timeStamp)
{
    currentBuffer_->append((char*)&len, sizeof(int));
    currentBuffer_->append((char*)&timeStamp, sizeof(timeval));
    currentBuffer_->append((const char*)data, len);
}

void TaskQueue2::consumBuffer(const BufferPtr &buf)
{
    int len;
    timeval timeStamp;
    ip *data;

    const char *begin = buf->data();
    const char *end = buf->current();

    auto &ipSingleton = muduo::ThreadLocalSingleton<IpFragment>::instance();

    while(begin != end) {
        len = *(const int*)begin;
        begin += sizeof(int);

        timeStamp = *(const timeval*)begin;
        begin += sizeof(timeval);

        // dangerous cast
        data = (ip*)begin;
        begin += len;

        ipSingleton.startIpfragProc(data, len, timeStamp);
    }

    assert(begin == end);
}