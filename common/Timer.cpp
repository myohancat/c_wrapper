/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#include "Timer.h"

#include "MainLoop.h"
#include "SysTime.h"
#include "Log.h"

uint64_t Timer::makeExpiry(uint32_t interval)
{
    if (interval == Infinite)
        return static_cast<uint64_t>(-1);

    return SysTime::getTickCountMs() + interval;
}

Timer::Timer(MainLoop& loop)
    : mLoop(loop)
    , mState(State::Stopped)
    , mStopRequested(false)
    , mExpiry(static_cast<uint64_t>(-1))
    , mHandler(nullptr)
    , mInterval(Infinite)
    , mRepeat(false)
{
}

Timer::~Timer()
{
    /*
     * Safe against MainLoop::runTimers() running on another thread.
     *
     * Forbidden:
     * - destroying this Timer from its own callback.
     * - destroying this Timer's owner from its own callback.
     */
    stopAndWait();
}

void Timer::setHandler(ITimerHandler* handler)
{
    Lock<Mutex> lock(mConfigLock);
    mHandler = handler;
}

void Timer::start(uint32_t msec, bool repeat)
{
    Lock<Mutex> controlLock(mControlLock);

    /*
     * start() is intentionally strong:
     * stop completely first, then schedule again.
     *
     * Do not call this on THIS timer from its callback.
     */
    stopAndWait();

    if (msec == Infinite)
    {
        LOGE("Timer::start() ignored. interval is Infinite.");
        return;
    }

    {
        Lock<Mutex> lock(mConfigLock);

        mInterval = msec;
        mRepeat = repeat;
        mExpiry.store(makeExpiry(mInterval), std::memory_order_release);
    }

    mStopRequested.store(false, std::memory_order_release);
    mState.store(State::Queued, std::memory_order_release);

    mLoop.addTimer(this);
}

void Timer::restart()
{
    uint32_t interval = Infinite;
    bool repeat = false;

    {
        Lock<Mutex> lock(mConfigLock);
        interval = mInterval;
        repeat = mRepeat;
    }

    start(interval, repeat);
}

void Timer::stop()
{
    Lock<Mutex> controlLock(mControlLock);

    while (true)
    {
        State state = mState.load(std::memory_order_acquire);

        switch (state)
        {
            case State::Stopped:
                return;

            case State::Queued:
            {
                if (mState.compare_exchange_weak(
                        state,
                        State::Stopped,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    mStopRequested.store(true, std::memory_order_release);
                    mLoop.removeTimer(this);
                    mStateChanged.broadcast();
                    return;
                }

                break;
            }

            case State::Executing:
            case State::RequeuePending:
                /*
                 * MainLoop currently owns the execution path for this timer.
                 * Do not touch MainLoop's timer list here.
                 */
                mStopRequested.store(true, std::memory_order_release);
                return;
        }
    }
}

void Timer::stopAndWait()
{
    /*
     * If caller already holds mControlLock, this function still works because
     * start() calls it while holding mControlLock.
     *
     * Therefore this function must not lock mControlLock internally.
     */
    while (true)
    {
        State state = mState.load(std::memory_order_acquire);

        switch (state)
        {
            case State::Stopped:
                return;

            case State::Queued:
            {
                if (mState.compare_exchange_weak(
                        state,
                        State::Stopped,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    mStopRequested.store(true, std::memory_order_release);
                    mLoop.removeTimer(this);
                    mStateChanged.broadcast();
                    return;
                }

                break;
            }

            case State::Executing:
            case State::RequeuePending:
            {
                /*
                 * MainLoop holds this Timer pointer outside the timer-list
                 * lock. Request stop and wait until MainLoop finishes using it.
                 *
                 * Calling this from this timer's own callback will deadlock.
                 * That usage is forbidden by ITimerHandler contract.
                 */
                mStopRequested.store(true, std::memory_order_release);

                Lock<Mutex> lock(mWaitLock);
                mStateChanged.wait(mWaitLock, [this]() {
                    State s = mState.load(std::memory_order_acquire);
                    return s != State::Executing &&
                           s != State::RequeuePending;
                });

                /*
                 * It may become Queued if MainLoop requeued it before seeing
                 * the stop request. Loop again and remove it safely.
                 */
                break;
            }
        }
    }
}

void Timer::setInterval(uint32_t msec)
{
    Lock<Mutex> lock(mConfigLock);
    mInterval = msec;
}

uint32_t Timer::getInterval() const
{
    Lock<Mutex> lock(mConfigLock);
    return mInterval;
}

void Timer::setRepeat(bool repeat)
{
    Lock<Mutex> lock(mConfigLock);
    mRepeat = repeat;
}

bool Timer::getRepeat() const
{
    Lock<Mutex> lock(mConfigLock);
    return mRepeat;
}

bool Timer::isRunning() const
{
    return mState.load(std::memory_order_acquire) != State::Stopped;
}

uint64_t Timer::getExpiryFromLoop() const
{
    return mExpiry.load(std::memory_order_acquire);
}

bool Timer::tryBeginExecuteFromLoop()
{
    State expected = State::Queued;

    if (!mState.compare_exchange_strong(
            expected,
            State::Executing,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        return false;
    }

    mStopRequested.store(false, std::memory_order_release);
    return true;
}

bool Timer::executeFromLoop()
{
    ITimerHandler* handler = nullptr;

    {
        Lock<Mutex> lock(mConfigLock);
        handler = mHandler;
    }

    bool keepByHandler = false;

    if (handler)
    {
        keepByHandler = handler->onTimerExpired(*this);
    }

    uint32_t interval = Infinite;
    bool repeat = false;
    bool hasHandler = false;

    {
        Lock<Mutex> lock(mConfigLock);

        interval = mInterval;
        repeat = mRepeat;
        hasHandler = (mHandler != nullptr);
    }

    const bool keep =
        !mStopRequested.load(std::memory_order_acquire) &&
        keepByHandler &&
        repeat &&
        interval != Infinite &&
        hasHandler;

    if (!keep)
    {
        mState.store(State::Stopped, std::memory_order_release);
        mStateChanged.broadcast();
        return false;
    }

    /*
     * Embedded-safe default:
     * next expiry is based on current time.
     *
     * This avoids catch-up storms when the system was delayed.
     */
    mExpiry.store(makeExpiry(interval), std::memory_order_release);

    mState.store(State::RequeuePending, std::memory_order_release);
    mStateChanged.broadcast();

    return true;
}

void Timer::requeueFromLoop()
{
    State state = mState.load(std::memory_order_acquire);

    if (state != State::RequeuePending)
    {
        mStateChanged.broadcast();
        return;
    }

    if (mStopRequested.load(std::memory_order_acquire))
    {
        mState.store(State::Stopped, std::memory_order_release);
        mStateChanged.broadcast();
        return;
    }

    /*
     * Keep RequeuePending while inserting into MainLoop.
     * This prevents stopAndWait() from returning while MainLoop still holds
     * this Timer pointer.
     */
    mLoop.addTimer(this);

    if (mStopRequested.load(std::memory_order_acquire))
    {
        mLoop.removeTimer(this);

        mState.store(State::Stopped, std::memory_order_release);
        mStateChanged.broadcast();
        return;
    }

    mState.store(State::Queued, std::memory_order_release);
    mStateChanged.broadcast();
}
