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
#include <muduo/base/ThreadPool.h>

class Dispatcher : muduo::noncopyable {

public:
    // not the same as Capture
    typedef std::function<void(ip*, int)> IpFragmentCallback;

    explicit
    Dispatcher(const std::vector<IpFragmentCallback>& cb, u_int queueSize);
    ~Dispatcher();

    void onIpFragment(const ip*, int);

private:

    u_int nWorkers_;
    u_int queueSize_;

    std::vector<IpFragmentCallback> callbacks_;

    std::vector<std::unique_ptr<muduo::ThreadPool>>  workers_;

    std::vector<int> taskCounter_;
};


#endif //NOFF_DISPATCHER_H
