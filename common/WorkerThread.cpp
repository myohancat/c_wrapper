/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#include "WorkerThread.h"

#include "Log.h"

#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <string.h>

WorkerThread::WorkerThread(const char* name, int priority, int cpuid)
    : mWorker(nullptr),
      mPriority(priority),
      mCpuId(cpuid),
      mId{},
      mState(ThreadState::Idle),
      mWakeupRequested(false)
{
    snprintf(mName, sizeof(mName), "%s", name);
}

WorkerThread::~WorkerThread() noexcept
{
    ThreadState state = mState.load();

    if (state == ThreadState::Idle)
        return;

    if (pthread_equal(pthread_self(), mId) != 0)
    {
        LOGE("[%s] WorkerThread destroyed from its own worker thread. State: %d", mName, static_cast<int>(state));
        std::abort();
    }

    stop();
}

void WorkerThread::setCpuAffinity(int cpuid)
{
    Lock<Mutex> lifecycleLock(mLifecycleLock);

    mCpuId = cpuid;

    if (cpuid == -1)
        return;

    ThreadState state = mState.load();

    if (state != ThreadState::Running && state != ThreadState::Stopping)
        return;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuid, &cpuset);

    int ret = pthread_setaffinity_np(mId, sizeof(cpu_set_t), &cpuset);
    if (ret != 0)
    {
        LOGW("[%s] Cannot pthread_setaffinity_np: %s", mName, strerror(ret));
    }
}

bool WorkerThread::start(IWorker& worker)
{
    Lock<Mutex> lifecycleLock(mLifecycleLock);

    ThreadState state = mState.load();

    if (state == ThreadState::Exited)
    {
        joinLocked();
        state = mState.load();
    }

    if (state != ThreadState::Idle)
    {
        LOGW("[%s] task is already running or stopping. State: %d", mName, static_cast<int>(state));
        return false;
    }

    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    if (ret != 0)
    {
        LOGW("[%s] Cannot pthread_attr_init: %s", mName, strerror(ret));
        return false;
    }

    if (mPriority > 0)
    {
        ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if (ret != 0)
        {
            LOGW("[%s] Cannot pthread_attr_setinheritsched: %s", mName, strerror(ret));
        }

        ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        if (ret != 0)
        {
            LOGW("[%s] Cannot pthread_attr_setschedpolicy: %s", mName, strerror(ret));
        }

        sched_param params{};
        params.sched_priority = mPriority;

        ret = pthread_attr_setschedparam(&attr, &params);
        if (ret != 0)
        {
            LOGW("[%s] Cannot pthread_attr_setschedparam: %s", mName, strerror(ret));
        }
    }

    if (!worker.onPreStart())
    {
        LOGE("[%s] onPreStart() failed", mName);
        pthread_attr_destroy(&attr);
        return false;
    }

    {
        Lock<Mutex> sleepLock(mSleepLock);
        mWakeupRequested = false;
    }

    {
        Lock<Mutex> lock(mLock);
        mWorker = &worker;
        mState.store(ThreadState::Running);
    }

    ret = pthread_create(&mId, &attr, _task_proc_priv, this);
    pthread_attr_destroy(&attr);

    if (ret != 0)
    {
        LOGE("[%s] pthread_create() failed: %s", mName, strerror(ret));

        Lock<Mutex> lock(mLock);
        mWorker = nullptr;
        mState.store(ThreadState::Idle);
        return false;
    }

    if (mCpuId != -1)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(mCpuId, &cpuset);

        ret = pthread_setaffinity_np(mId, sizeof(cpu_set_t), &cpuset);
        if (ret != 0)
        {
            LOGW("[%s] Cannot pthread_setaffinity_np: %s", mName, strerror(ret));
        }
    }

    char nameBuf[16];
    strncpy(nameBuf, mName, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';

    ret = pthread_setname_np(mId, nameBuf);
    if (ret != 0)
    {
        LOGW("[%s] Cannot pthread_setname_np: %s", mName, strerror(ret));
    }

    return true;
}

void WorkerThread::requestStop() noexcept
{
    ThreadState state = mState.load();

    if (state == ThreadState::Idle || state == ThreadState::Exited)
        return;

    ThreadState expected = ThreadState::Running;

    if (mState.compare_exchange_strong(expected, ThreadState::Stopping))
    {
        IWorker* worker = nullptr;

        {
            Lock<Mutex> lock(mLock);
            worker = mWorker;
        }

        if (worker != nullptr)
        {
            worker->onPreStop();
        }
    }

    wakeup();
}

void WorkerThread::join()
{
    ThreadState state = mState.load();

    if (state == ThreadState::Idle)
        return;

    if (pthread_equal(pthread_self(), mId) != 0)
    {
        LOGE("[%s] WorkerThread::join() called from its own worker thread. State: %d", mName, static_cast<int>(state));
        std::abort();
    }

    Lock<Mutex> lifecycleLock(mLifecycleLock);

    state = mState.load();

    if (state == ThreadState::Idle)
        return;

    joinLocked();
}

void WorkerThread::stop()
{
    requestStop();
    join();
}

void WorkerThread::joinLocked()
{
    int ret = pthread_join(mId, nullptr);
    if (ret != 0)
    {
        LOGE("[%s] pthread_join() failed: %s", mName, strerror(ret));
        return;
    }

    {
        Lock<Mutex> lock(mLock);
        mWorker = nullptr;
        mState.store(ThreadState::Idle);
    }
}

void WorkerThread::msleep(int msec)
{
    if (msec <= 0)
        return;

    ThreadState state = mState.load();

    if ((state == ThreadState::Running || state == ThreadState::Stopping) &&
        pthread_equal(pthread_self(), mId) != 0)
    {
        Lock<Mutex> lock(mSleepLock);

        if (mState.load() == ThreadState::Stopping)
            return;

        mCvSleep.wait(mSleepLock, msec, [this]() {
            return mState.load() == ThreadState::Stopping || mWakeupRequested;
        });

        mWakeupRequested = false;
        return;
    }

    timespec req;
    req.tv_sec = msec / 1000;
    req.tv_nsec = static_cast<long>(msec % 1000) * 1000000L;

    while (nanosleep(&req, &req) != 0)
    {
        if (errno != EINTR)
            break;
    }
}

void WorkerThread::wakeup()
{
    {
        Lock<Mutex> lock(mSleepLock);
        mWakeupRequested = true;
    }

    mCvSleep.broadcast();
}

void* WorkerThread::_task_proc_priv(void* param) noexcept
{
    WorkerThread* pThis = static_cast<WorkerThread*>(param);

    IWorker* worker = nullptr;

    {
        Lock<Mutex> lock(pThis->mLock);
        worker = pThis->mWorker;
    }

    if (worker != nullptr)
    {
        worker->onPostStart();
        worker->run();
        worker->onPostStop();
    }

    ThreadState state = pThis->mState.load();

    if (state == ThreadState::Running ||
        state == ThreadState::Stopping)
    {
        pThis->mState.store(ThreadState::Exited);
    }

    return nullptr;
}
