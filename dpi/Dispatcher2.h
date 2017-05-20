//
// Created by root on 17-5-20.
//

#ifndef NOFF_DISPATCHER2_H
#define NOFF_DISPATCHER2_H

#include <functional>
#include <vector>
#include <memory>
#include <netinet/ip.h>

#include <muduo/base/noncopyable.h>

#include "TaskQueue2.h"

class Dispatcher2 : muduo::noncopyable
{
public:
    //same as Capture
    typedef std::function<void()> ThreadInitCallback;

    explicit
    Dispatcher2(u_int nWorkers, const ThreadInitCallback &cb);
    ~Dispatcher2();

    void onIpFragment(const ip*, int, timeval);

private:

    u_int nWorkers_;

    ThreadInitCallback threadInitCallback_;

    std::vector<std::unique_ptr<TaskQueue2>>  workers_;

    std::vector<int> taskCounter_;
};


#endif //NOFF_DISPATCHER2_H
