/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

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
    Mutex() { pthread_mutex_init(&mId, nullptr); }
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
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mId, &attr);
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
