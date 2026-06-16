/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include <Mutex.h>
#include <CondVar.h>

#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>

#include <atomic>

/*
 * Usage:
 *
 * class Foo : public IWorker
 * {
 * public:
 *     // Stop the worker before Foo's members are destroyed.
 *     ~Foo() { stop(); }
 *
 *     bool start() { return mThread.start(*this); }
 *     void stop()  { mThread.stop(); }
 *
 * protected:
 *     void run() noexcept override
 *     {
 *         while (mThread.shouldRun())
 *         {
 *             // TODO
 *             mThread.msleep(1000);
 *         }
 *     }
 *
 * private:
 *     // Members are destroyed in reverse declaration order.
 *     // Keep resources used by run() before WorkerThread so they outlive it.
 *     Buffer mBuffer;
 *     WorkerThread mThread;
 * };
 */

class IWorker
{
public:
    virtual ~IWorker() noexcept = default;

    /*
     * Called by the worker thread.
     */
    virtual void run() noexcept = 0;


    /*
     * Called by the start() caller before pthread_create().
     * Note : Must not call start(), stop(), requestStop(), or join().
     */
    virtual bool onPrepare() noexcept { return true; }

    /*
     * Called once after the state changes to Stopping.
     * Note : Must not call start(), stop(), requestStop(), or join().
     */
    virtual void onStopRequested() noexcept {}

    /*
     * Called after pthread_join() succeeds or pthread_create() fails after onPrepare() succeeds.
     * Note : Must not call start(), stop(), requestStop(), or join().
     */
    virtual void onCleanup() noexcept {}
};


class WorkerThread
{
public:
    explicit WorkerThread(const char* name = "Worker", int priority = -1);

    ~WorkerThread() noexcept;

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    bool start(IWorker& worker);
    void stop(); // requestStop() + join()

    void requestStop() noexcept;
    void join();

    void sleep(int sec);
    void msleep(int msec);

    bool shouldRun() const noexcept;
    bool isCurrentThread() const noexcept;

private:
    enum class ThreadState : uint8_t
    {
        Idle,
        Starting,
        Running,
        Stopping,
        Exited
    };

private:
    static void* taskEntry(void* param) noexcept;

    // Caller must hold mLifecycleLock.
    void joinLocked();

    // Caller must hold mStateLock.
    void resetStateLocked() noexcept;

private:
    IWorker* mWorker;

    int mPriority;
    char mName[32];
    pthread_t mId;

    // Serializes start()/stop()/join() and onCleanup().
    Mutex mLifecycleLock;

    // Protects mId, mWorker, all flags, and all state transitions.
    mutable Mutex mStateLock;
    CondVar mStateCv;
    CondVar mSleepCv;

    std::atomic<ThreadState> mState;

    bool mStopRequested;
    bool mThreadReady;
    bool mStartReleased;
    bool mWakeupRequested;

    uint32_t mCallbacksInProgress;
};

inline void WorkerThread::sleep(int sec)
{
    if (sec <= 0)
        return;

    if (sec > INT_MAX / 1000)
        sec = INT_MAX / 1000;

    msleep(sec * 1000);
}

inline bool WorkerThread::shouldRun() const noexcept
{
    return mState.load(std::memory_order_acquire) == ThreadState::Running;
}

inline bool WorkerThread::isCurrentThread() const noexcept
{
    Lock<Mutex> lock(mStateLock);

    const ThreadState state = mState.load(std::memory_order_relaxed);

    if (state != ThreadState::Running &&
        state != ThreadState::Stopping)
    {
        return false;
    }

    return pthread_equal(pthread_self(), mId) != 0;
}
