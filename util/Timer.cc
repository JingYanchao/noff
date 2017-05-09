#include "Timer.h"

//
// Created by root on 17-5-9.
//
#include <muduo/base/Mutex.h>

bool Timer::checkTime(timeval TimeStamp)
{
    bool timeout = false;
    {
        muduo::MutexLockGuard guard(lock);
        if(time<=TimeStamp.tv_sec)
        {
            timeout = true;
            time= TimeStamp.tv_sec+1;
        }
    }
    return timeout;
}