//
// Created by root on 17-4-13.
//

#ifndef NOFF_DISPATCHER_H
#define NOFF_DISPATCHER_H

#include <functional>
#include <vector>
#include <memory>
#include <netinet/ip.h>

#include <muduo/base/noncopyable.h>

#include "TaskQueue.h"

class Dispatcher : muduo::noncopyable {

public:
    //same as Capture
    typedef std::function<void(ip*, int, timeval)> IpFragmentCallback;
    typedef std::function<void()> ThreadInitCallback;

    explicit
    Dispatcher(u_int nWorkers, u_int queueSize, const ThreadInitCallback &cb);
    ~Dispatcher();

    void onIpFragment(const ip*, int, timeval);

private:

    u_int nWorkers_;

    ThreadInitCallback threadInitCallback_;

    std::vector<std::unique_ptr<TaskQueue>>  workers_;

    std::vector<int> taskCounter_;
};


#endif //NOFF_DISPATCHER_H
