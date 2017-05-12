#include "Timer.h"

//
// Created by root on 17-5-9.
//
#include <pthread.h>
#include <errno.h>

bool Timer::checkTime(timeval TimeStamp)
{
    pthread_mutex_t *mutex = lock.getPthreadMutex();

    int err = pthread_mutex_trylock(mutex);

    if (err != 0) {
        return false;
    }

    bool timeout = false;
    if (time <= TimeStamp.tv_sec) {
        time = TimeStamp.tv_sec + 1;
        timeout = true;
    }

    pthread_mutex_unlock(mutex);
    return timeout;
}