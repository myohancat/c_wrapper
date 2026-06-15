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

    virtual void run() noexcept = 0;

    /*
     * Callback thread context:
     *
     * onPreStart()  : called by the thread that calls start(), before pthread_create().
     * onPostStart() : called by the thread that calls start(), after pthread_crate().
     * onPreStop()   : called by the thread that requests stop().
     * onPostStop()  : called by the worker thread, after run().
     */
    virtual bool onPreStart() noexcept { return true; }
    virtual void onPreStop() noexcept {}

    virtual void onPostStart() noexcept {}
    virtual void onPostStop() noexcept {}
};

class WorkerThread
{
public:
    WorkerThread(const char* name = "Worker",
                 int priority = -1,
                 int cpuid = -1);

    ~WorkerThread() noexcept;

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    void setCpuAffinity(int cpuid);
    int getCpuAffinity() const;

    bool start(IWorker& worker);

    /*
     * Requests the worker thread to stop.
     *
     * This does not join.
     * It changes Running -> Stopping, calls onPreStop(), and wakes the worker.
     * Safe to call from the worker thread itself.
     */
    void requestStop() noexcept;

    /*
     * Waits for the worker thread to finish and joins it.
     *
     * This does not request stop.
     * If the worker keeps running, this call blocks indefinitely.
     * Must not be called from the worker thread itself.
     */
    void join();

    /*
     * requestStop() + join()
     */
    void stop();

    void sleep(int sec);
    void msleep(int msec);

    void wakeup();

    bool shouldRun() const noexcept;
    bool isCurrentThread() const noexcept;

private:
    enum class ThreadState : uint8_t
    {
        Idle,
        Running,
        Stopping,
        Exited
    };

private:
    static void* _task_proc_priv(void* param) noexcept;

    void joinLocked();

private:
    IWorker* mWorker;
    int mPriority;
    int mCpuId;
    char mName[32];
    pthread_t mId;

    Mutex mLifecycleLock;
    Mutex mLock;
    std::atomic<ThreadState> mState;

    bool mIsRunEntered;
    Mutex mStartLock;
    CondVar mCvStart;

    bool mWakeupRequested;
    mutable Mutex mSleepLock;
    CondVar mCvSleep;
};

inline int WorkerThread::getCpuAffinity() const
{
    return mCpuId;
}

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
    return mState.load() == ThreadState::Running;
}

inline bool WorkerThread::isCurrentThread() const noexcept
{
    ThreadState state = mState.load();

    return (state == ThreadState::Running || state == ThreadState::Stopping) &&
           pthread_equal(pthread_self(), mId) != 0;
}
