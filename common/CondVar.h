/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "Mutex.h"
#include "SysTime.h"
#include "Log.h"

#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

class CondVar
{
public:
    CondVar()
    {
        pthread_condattr_t attr;

        int rc = pthread_condattr_init(&attr);
        ABORT_IF(rc != 0);

        rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        ABORT_IF(rc != 0);

        rc = pthread_cond_init(&mId, &attr);
        ABORT_IF(rc != 0);

        rc = pthread_condattr_destroy(&attr);
        ABORT_IF(rc != 0);
    }

    ~CondVar()
    {
        pthread_cond_destroy(&mId);
    }

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    void wait(Mutex& mutex)
    {
        int rc = pthread_cond_wait(&mId, &mutex.mId);
        ABORT_IF(rc != 0);
    }

    bool wait(Mutex& mutex, int timeoutMs)
    {
        if (timeoutMs < 0)
        {
            wait(mutex);
            return true;
        }

        uint64_t expireTime = SysTime::getTickCountMs() + static_cast<uint64_t>(timeoutMs);

        return waitUntil(mutex, expireTime);
    }

    template <typename Predicate>
    void wait(Mutex& mutex, Predicate pred)
    {
        while (!pred())
        {
            wait(mutex);
        }
    }

    template <typename Predicate>
    bool wait(Mutex& mutex, int timeoutMs, Predicate pred)
    {
        if (timeoutMs < 0)
        {
            wait(mutex, pred);
            return true;
        }

        uint64_t expireTime = SysTime::getTickCountMs() + static_cast<uint64_t>(timeoutMs);

        while (!pred())
        {
            if (!waitUntil(mutex, expireTime))
            {
                return pred();
            }
        }
        return true;
    }

    void signal()
    {
        int rc = pthread_cond_signal(&mId);
        ABORT_IF(rc != 0);
    }

    void broadcast()
    {
        int rc = pthread_cond_broadcast(&mId);
        ABORT_IF(rc != 0);
    }

private:
    bool waitUntil(Mutex& mutex, const uint64_t expireTimeMs)
    {
        struct timespec ts {};
        ts.tv_sec  = expireTimeMs / 1000;
        ts.tv_nsec = (expireTimeMs % 1000) * 1000000L;

        const int rc = pthread_cond_timedwait(&mId, &mutex.mId, &ts);

        if (rc == 0) return true;
        if (rc == ETIMEDOUT) return false;

        LOGE("pthread_cond_timedwait failed: rc=%d, error=%s", rc, strerror(rc));
        ABORT_IF(rc != 0);

        return false;
    }

private:
    pthread_cond_t mId;
};
