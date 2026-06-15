/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#include "TimerThread.h"

#include "SysTime.h"
#include "Log.h"

TimerThread::TimerThread()
          : mRepeat(false),
            mStartTime(0),
            mIntervalMs(0),
            mHandler(nullptr),
            mThread("TimerThread")
{
}

TimerThread::~TimerThread()
{
    stop();
}

void TimerThread::setHandler(ITimerHandler* handler)
{
    Lock<RecursiveMutex> lock(mLock);

    mHandler = handler;
}

void TimerThread::start(uint32_t msec, bool repeat)
{
    mThread.stop();

    {
        Lock<RecursiveMutex> lock(mLock);
        mRepeat     = repeat;
        mStartTime  = SysTime::getTickCountMs();
        mIntervalMs = msec;
    }

    if (!mThread.start(*this))
    {
        LOGE("failed to create task");
    }
}

void TimerThread::stop()
{
    mThread.stop();
}

void TimerThread::setInterval(uint32_t msec)
{
    Lock<RecursiveMutex> lock(mLock);
    mIntervalMs = msec;
}

void TimerThread::setRepeat(bool repeat)
{
    Lock<RecursiveMutex> lock(mLock);
    mRepeat = repeat;
}

uint32_t TimerThread::getInterval() const
{
    Lock<RecursiveMutex> lock(mLock);
    return mIntervalMs;
}

bool TimerThread::getRepeat() const
{
    Lock<RecursiveMutex> lock(mLock);
    return mRepeat;
}

void TimerThread::restart()
{
    mThread.stop();
    {
        Lock<RecursiveMutex> lock(mLock);
        mStartTime  = SysTime::getTickCountMs();
    }
    mThread.start(*this);
}

void TimerThread::run() noexcept
{
    while (mThread.shouldRun())
    {
        int timeoutMs = 0;
        {
            Lock<RecursiveMutex> lock(mLock);

            const int64_t currentTime = SysTime::getTickCountMs();
            const int64_t expireTime  = mStartTime + mIntervalMs;
            const int64_t remainMs    = expireTime - currentTime;

            if (remainMs <= 0)
            {
                LOGW("too short remainMs : %lld, change to 1ms", static_cast<long long>(remainMs));
                timeoutMs = 1; // avoid busy waiting.
            }
            else if (remainMs > INT32_MAX)
                timeoutMs = INT32_MAX;
            else
                timeoutMs = static_cast<int>(remainMs);
        }

        mThread.msleep(timeoutMs);

        {
            Lock<RecursiveMutex> lock(mLock);

            if (!mThread.shouldRun())
                break;

            bool keepGoing = mRepeat;

            if (mHandler)
                keepGoing &= mHandler->onTimerExpired(*this);

            if (!keepGoing)
                break;

            mStartTime += mIntervalMs;
        }
    }
}
