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

#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <string.h>

WorkerThread::WorkerThread(const char* name, int priority)
    : mWorker(nullptr),
      mPriority(priority),
      mName{},
      mId{},
      mState(ThreadState::Idle),
      mStopRequested(false),
      mThreadReady(false),
      mStartReleased(false),
      mWakeupRequested(false),
      mCallbacksInProgress(0)
{
    snprintf(mName, sizeof(mName), "%s", name != nullptr ? name : "Worker");
}

WorkerThread::~WorkerThread() noexcept
{
    if (isCurrentThread())
    {
        LOGE("[%s] WorkerThread destroyed from its own worker thread", mName);
        ABORT_IF(true);
    }

    stop();
}

void WorkerThread::resetStateLocked() noexcept
{
    mWorker = nullptr;
    mId = pthread_t{};

    mStopRequested = false;
    mThreadReady = false;
    mStartReleased = false;
    mWakeupRequested = false;
    mCallbacksInProgress = 0;

    mState.store(ThreadState::Idle, std::memory_order_release);
}

bool WorkerThread::start(IWorker& worker)
{
    Lock<Mutex> lifecycleLock(mLifecycleLock);

    if (mState.load(std::memory_order_acquire) == ThreadState::Exited)
        joinLocked();

    {
        Lock<Mutex> stateLock(mStateLock);

        const ThreadState state = mState.load(std::memory_order_relaxed);
        if (state != ThreadState::Idle)
        {
            LOGW("[%s] Worker is already active. State: %d", mName, static_cast<int>(state));
            return false;
        }

        mWorker = &worker;
        mId = pthread_t{};

        mStopRequested = false;
        mThreadReady = false;
        mStartReleased = false;
        mWakeupRequested = false;
        mCallbacksInProgress = 0;

        mState.store(ThreadState::Starting, std::memory_order_release);
    }

    const auto resetToIdle = [this]() noexcept {
        Lock<Mutex> stateLock(mStateLock);
        resetStateLocked();
        mStateCv.broadcast();
        mSleepCv.broadcast();
    };

    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    if (ret != 0)
    {
        LOGE("[%s] pthread_attr_init() failed: %s", mName, strerror(ret));
        resetToIdle();
        return false;
    }

    if (mPriority > 0)
    {
        ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if (ret != 0)
            LOGW("[%s] pthread_attr_setinheritsched() failed: %s", mName, strerror(ret));

        ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        if (ret != 0)
            LOGW("[%s] pthread_attr_setschedpolicy() failed: %s", mName, strerror(ret));

        sched_param param{};
        param.sched_priority = mPriority;

        ret = pthread_attr_setschedparam(&attr, &param);
        if (ret != 0)
            LOGW("[%s] pthread_attr_setschedparam() failed: %s", mName, strerror(ret));
    }

    if (!worker.onPrepare())
    {
        LOGE("[%s] onPrepare() failed", mName);
        pthread_attr_destroy(&attr);
        resetToIdle();
        return false;
    }

    pthread_t createdId{};
    ret = pthread_create(&createdId, &attr, &WorkerThread::taskEntry, this);
    pthread_attr_destroy(&attr);

    if (ret != 0)
    {
        LOGE("[%s] pthread_create() failed: %s", mName, strerror(ret));
        resetToIdle();
        worker.onCleanup();
        return false;
    }

    IWorker* stopCallbackWorker = nullptr;
    {
        Lock<Mutex> stateLock(mStateLock);

        mId = createdId;

        mStateCv.wait(mStateLock, [this]() {
            return mThreadReady;
        });

        if (mStopRequested)
        {
            mState.store(ThreadState::Stopping, std::memory_order_release);
            stopCallbackWorker = mWorker;

            if (stopCallbackWorker != nullptr)
                ++mCallbacksInProgress;

            mStartReleased = true;
            mWakeupRequested = true;
            mSleepCv.broadcast();
        }
        else
        {
            mState.store(ThreadState::Running, std::memory_order_release);
            mStartReleased = true;
        }
        mStateCv.broadcast();
    }

    if (stopCallbackWorker != nullptr)
    {
        stopCallbackWorker->onStopRequested();
        {
            Lock<Mutex> stateLock(mStateLock);

            if (mCallbacksInProgress > 0)
                --mCallbacksInProgress;

            mStateCv.broadcast();
        }
    }

    return true;
}

void WorkerThread::requestStop() noexcept
{
    IWorker* workerToNotify = nullptr;

    {
        Lock<Mutex> stateLock(mStateLock);

        const ThreadState state = mState.load(std::memory_order_relaxed);

        if (state == ThreadState::Idle || state == ThreadState::Exited)
            return;

        mStopRequested = true;
        mWakeupRequested = true;

        if (state == ThreadState::Running)
        {
            mState.store(ThreadState::Stopping, std::memory_order_release);
            workerToNotify = mWorker;

            if (workerToNotify != nullptr)
                ++mCallbacksInProgress;
        }

        mStateCv.broadcast();
        mSleepCv.broadcast();
    }

    if (workerToNotify != nullptr)
    {
        workerToNotify->onStopRequested();
        {
            Lock<Mutex> stateLock(mStateLock);

            if (mCallbacksInProgress > 0)
                --mCallbacksInProgress;

            mStateCv.broadcast();
        }
    }
}

void WorkerThread::join()
{
    if (isCurrentThread())
    {
        LOGE("[%s] join() called from its own worker thread", mName);
        return;
    }

    Lock<Mutex> lifecycleLock(mLifecycleLock);

    if (mState.load(std::memory_order_acquire) == ThreadState::Idle)
        return;

    joinLocked();
}

void WorkerThread::stop()
{
    if (isCurrentThread())
    {
        requestStop();
        return;
    }

    Lock<Mutex> lifecycleLock(mLifecycleLock);

    requestStop();

    if (mState.load(std::memory_order_acquire) != ThreadState::Idle)
        joinLocked();
}

void WorkerThread::joinLocked()
{
    pthread_t threadId{};

    {
        Lock<Mutex> stateLock(mStateLock);

        const ThreadState state = mState.load(std::memory_order_relaxed);
        if (state == ThreadState::Idle)
            return;

        if (state == ThreadState::Starting)
        {
            LOGE("[%s] joinLocked() observed an incomplete start", mName);
            return;
        }

        threadId = mId;
    }

    const int ret = pthread_join(threadId, nullptr);
    if (ret != 0)
    {
        LOGE("[%s] pthread_join() failed: %s", mName, strerror(ret));
        return;
    }

    IWorker* workerToCleanup = nullptr;

    {
        Lock<Mutex> stateLock(mStateLock);

        mStateCv.wait(mStateLock, [this]() {
            return mCallbacksInProgress == 0;
        });

        workerToCleanup = mWorker;
        mWorker = nullptr;
    }

    if (workerToCleanup != nullptr)
        workerToCleanup->onCleanup();

    {
        Lock<Mutex> stateLock(mStateLock);
        resetStateLocked();
        mStateCv.broadcast();
        mSleepCv.broadcast();
    }
}

void WorkerThread::msleep(int msec)
{
    if (msec <= 0)
        return;

    {
        Lock<Mutex> stateLock(mStateLock);

        const ThreadState state = mState.load(std::memory_order_relaxed);
        const bool currentWorker =
            (state == ThreadState::Running || state == ThreadState::Stopping) &&
            pthread_equal(pthread_self(), mId) != 0;

        if (currentWorker)
        {
            if (state == ThreadState::Stopping)
                return;

            mSleepCv.wait(mStateLock, msec, [this]() {
                return mState.load(std::memory_order_relaxed) != ThreadState::Running ||
                       mWakeupRequested;
            });

            mWakeupRequested = false;
            return;
        }
    }

    timespec request{};
    request.tv_sec = msec / 1000;
    request.tv_nsec = static_cast<long>(msec % 1000) * 1000000L;

    while (nanosleep(&request, &request) != 0)
    {
        if (errno != EINTR)
            break;
    }
}

void* WorkerThread::taskEntry(void* param) noexcept
{
    WorkerThread* self = static_cast<WorkerThread*>(param);

    {
        char nameBuf[16];
        strncpy(nameBuf, self->mName, sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';

        const int nameResult = pthread_setname_np(pthread_self(), self->mName);
        if (nameResult != 0)
            LOGW("[%s] pthread_setname_np() failed: %s", self->mName, strerror(nameResult));
    }

    IWorker* worker = nullptr;

    {
        Lock<Mutex> stateLock(self->mStateLock);

        self->mThreadReady = true;
        self->mStateCv.broadcast();

        self->mStateCv.wait(self->mStateLock, [self]() {
            return self->mStartReleased;
        });

        worker = self->mWorker;
    }

    if (worker != nullptr)
        worker->run();

    {
        Lock<Mutex> stateLock(self->mStateLock);

        self->mState.store(ThreadState::Exited, std::memory_order_release);
        self->mStateCv.broadcast();
        self->mSleepCv.broadcast();
    }

    return nullptr;
}
