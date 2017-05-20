//
// Created by root on 17-5-20.
//

#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Exception.h>
#include <muduo/base/ThreadLocalSingleton.h>

#include "IpFragment.h"
#include "Dispatcher2.h"
#include "Sharding.h"

namespace
{

Sharding shard;

};


Dispatcher2::Dispatcher2(u_int nWorkers, const ThreadInitCallback& cb )
        :nWorkers_(nWorkers),
         threadInitCallback_(cb),
         taskCounter_(nWorkers_)
{
    workers_.reserve(nWorkers_);
    for (size_t i = 0; i < nWorkers_; ++i)
    {
        char name[32];
        snprintf(name, sizeof name, "%s%lu", "worker", i + 1);
        workers_.emplace_back(new TaskQueue2(name));
        workers_[i]->setThreadInitCallback(cb);
        workers_[i]->start();
    }

    LOG_INFO << "Dispatcher2: started, " << nWorkers_ << " workers";
}

Dispatcher2::~Dispatcher2()
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

void Dispatcher2::onIpFragment(const ip *hdr, int len, timeval timeStamp)
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

    worker.append(hdr, len, timeStamp);

    ++taskCounter_[index];
}
