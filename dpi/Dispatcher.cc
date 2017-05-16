//
// Created by root on 17-4-13.
//

#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Exception.h>
#include <muduo/base/ThreadLocalSingleton.h>

#include "IpFragment.h"
#include "Dispatcher.h"
#include "Sharding.h"

namespace
{

Sharding shard;

};


Dispatcher::Dispatcher(u_int nWorkers, u_int queueSize, const ThreadInitCallback& cb )
    :nWorkers_(nWorkers),
     threadInitCallback_(cb),
     taskCounter_(nWorkers_)
{
    workers_.reserve(nWorkers_);
    for (size_t i = 0; i < nWorkers_; ++i)
    {
        char name[32];
        snprintf(name, sizeof name, "%s%lu", "worker", i + 1);
        workers_.emplace_back(new TaskQueue(name));
        workers_[i]->setThreadInitCallback(cb);
        workers_[i]->setMaxQueueSize(queueSize);
        workers_[i]->start();
    }

    LOG_INFO << "Dispatcher: started, " << nWorkers_ << " workers";
}

Dispatcher::~Dispatcher()
{
    for (size_t i = 0; i < nWorkers_; ++i) {
        LOG_INFO << workers_[i]->name() << ": "
                  << taskCounter_[i];
    }

    /* not neccesarry */
//    for (auto& w : workers_) {
//        w->stop();
//    }
}

void Dispatcher::onIpFragment(const ip *hdr, int len, timeval timeStamp)
{
    if (len < hdr->ip_hl * 4) {
        LOG_WARN << "Dispatcher: " << "IP fragment too short";
        return;
    }

    u_int index = 0;

    try {
        index = shard(hdr, len) % nWorkers_;
    }
    catch (const muduo::Exception &ex) {
        // not UDP, TCP, ICMP protocols, this is usual
        LOG_TRACE << "Dispatcher: " << ex.what();
        return;
    }
    catch (...) {
        LOG_FATAL << "Dispatcher: unknown error";
    }

    auto& worker = *workers_[index];

    // necessary malloc and memcpy
    ip *copiedIpFragment = (ip*) malloc(len);
    if (copiedIpFragment == NULL) {
        LOG_FATAL << "Dispatcher: malloc failed";
    }
    memmove(copiedIpFragment, hdr, len);

    // should not block
    bool success = worker.nonBlockingRun([=]() {
        auto &ip = muduo::ThreadLocalSingleton<IpFragment>::instance();
        ip.startIpfragProc(copiedIpFragment, len, timeStamp);
        free(copiedIpFragment);
    });

    if (!success) {
        LOG_WARN << "Dispatcher: " << worker.name() << " overloaded";
    }
    else {
        ++taskCounter_[index];
    }
}

void Dispatcher::runTask(const Task &t)
{
    for (auto& w : workers_) {
        w->run(t);
    }
}