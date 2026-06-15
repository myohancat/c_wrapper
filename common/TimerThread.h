/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "Timer.h"
#include "Mutex.h"
#include "WorkerThread.h"

class TimerThread : public ITimer, IWorker
{
public:
    TimerThread();
    ~TimerThread();

    void     setHandler(ITimerHandler* handler) override;

    void     start(uint32_t msec, bool repeat) override;
    void     restart() override;
    void     stop() override;

    void     setInterval(uint32_t msec) override;
    uint32_t getInterval() const override;

    void     setRepeat(bool repeat) override;
    bool     getRepeat() const override;

    bool     isRunning() const override;

private:
    ITimerHandler*  mHandler;

    bool     mRepeat;
    uint64_t mStartTime;
    int      mIntervalMs;

private:
    void     run() noexcept override;

private:
    mutable RecursiveMutex mLock;
    WorkerThread mThread;
};

inline bool TimerThread::isRunning() const
{
    return mThread.shouldRun();
}
