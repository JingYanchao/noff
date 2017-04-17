//
// Created by root on 17-4-13.
//

#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Exception.h>

#include "Dispatcher.h"
#include "Sharding.h"

namespace
{

Sharding shard;

};


Dispatcher::Dispatcher(const std::vector<IpFragmentCallback>& cb, u_int queueSize)
    :nWorkers_((u_int)cb.size()),
     queueSize_(queueSize),
     callbacks_(cb),
     taskCounter_(nWorkers_)
{
    workers_.reserve(nWorkers_);
    for (size_t i = 0; i < nWorkers_; ++i) {

        char name[32];
        snprintf(name, sizeof name, "%s%lu", "worker", i + 1);

        workers_.emplace_back(new muduo::ThreadPool(name));
        workers_[i]->setMaxQueueSize(queueSize_);
        workers_[i]->start(1);
    }
    LOG_INFO << "Dispatcher: started, " << nWorkers_ << " workers";
}

Dispatcher::~Dispatcher()
{
    for (auto& w : workers_) {
        w->stop();
    }
    for (size_t i = 0; i < nWorkers_; ++i) {
        LOG_INFO << workers_[i]->name() << ": "
                  << taskCounter_[i];
    }
}

void Dispatcher::onIpFragment(const ip *hdr, int len, timeval timeStamp)
{
    if (len < hdr->ip_hl * 4) {
        LOG_WARN << "Dispatcher: " << "IP fragment too short";
        return;
    }

    u_int index;

    try {
        index = shard(hdr, len) % nWorkers_;
    }
    catch (const muduo::Exception &ex) {
        // not UDP, TCP, ICMP protocals, this is usual
        LOG_DEBUG << "Dispatcher: " << ex.what();
        return;
    }
    catch (...) {
        LOG_FATAL << "Dispatcher: unknown error";
    }

    auto& worker = *workers_[index];
    auto& callback = callbacks_[index];

    if (worker.queueSize() >= queueSize_ - 1) {
        LOG_WARN << "Dispatcher: " << worker.name() << " overloaded";
        return;
    }

    // necessary malloc and memcpy
    ip *copiedIpFragment = (ip*) malloc(len);
    if (copiedIpFragment == NULL)
    {
        LOG_FATAL << "Dispatcher: malloc failed";
    }

    memmove(copiedIpFragment, hdr, len);

    // should not block
    worker.run(std::bind(callback, copiedIpFragment, len, timeStamp));
    ++taskCounter_[index];

    // after task is complete, free pointer
    worker.run([=]() {
        free(copiedIpFragment);
    });
}