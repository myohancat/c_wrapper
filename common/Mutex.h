/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "Log.h"
#include <pthread.h>

/**
 * Lockable must implement
 * lock(), unlock()
 */
template <typename Lockable>
class Lock
{
public:
    explicit Lock(Lockable& lock) : mLock(lock)  { mLock.lock(); }
    ~Lock() { mLock.unlock(); }

private:
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    Lockable& mLock;
};

class Mutex
{
friend class CondVar;

public:
    Mutex()
    {
        int rc = pthread_mutex_init(&mId, nullptr);
        ABORT_IF(rc == 0);
    }

    ~Mutex() { pthread_mutex_destroy(&mId); }

    void lock() { pthread_mutex_lock(&mId); }
    void unlock() { pthread_mutex_unlock(&mId); }

private:
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
    pthread_mutex_t mId;
};

class RecursiveMutex
{
public:
    RecursiveMutex()
    {
        pthread_mutexattr_t attr;

        int ret = pthread_mutexattr_init(&attr);
        ABORT_IF(ret != 0);

        ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        ABORT_IF(ret != 0);

        ret = pthread_mutex_init(&mId, &attr);
        ABORT_IF(ret != 0);

        pthread_mutexattr_destroy(&attr);
    }

    ~RecursiveMutex() { pthread_mutex_destroy(&mId); }

    void lock() { pthread_mutex_lock(&mId); }
    void unlock() { pthread_mutex_unlock(&mId); }

private:
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;

private:
    pthread_mutex_t mId;
};
