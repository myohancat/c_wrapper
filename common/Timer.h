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
#include "CondVar.h"
#include <stdint.h>

#include <atomic>

class ITimer;

class ITimerHandler
{
public:
    virtual ~ITimerHandler() = default;

    /*
     * Called when the timer expires.
     *
     * Return value:
     * - true  : continue THIS timer if repeat mode is enabled.
     * - false : stop THIS timer after this callback returns.
     *
     * Important:
     * - This callback is called from the timer's execution context.
     *   For Timer, it is called from MainLoop.
     *   For TimerTask, it may be called from the timer task thread.
     *
     * - Do not call start(), restart(), stop(), stopAndWait(), or destroy
     *   on THIS timer from this callback.
     *
     * - Starting/stopping/restarting OTHER timers is allowed.
     *
     * - To stop THIS timer from inside this callback, return false.
     * - To continue THIS timer, return true.
     *
     * - Do not destroy this timer or its owner from this callback.
     *   If destruction is needed, defer it using MainLoop::post() or an
     *   equivalent mechanism.
     *
     * - The timer reference is non-owning.
     * - The timer reference is valid only during this call.
     */
    virtual bool onTimerExpired(const ITimer& timer) = 0;
};

class ITimer
{
public:
    virtual ~ITimer() = default;

    bool operator==(const ITimer& other) const
    {
        return this == &other;
    }

    bool operator!=(const ITimer& other) const
    {
        return this != &other;
    }

    virtual void setHandler(ITimerHandler* handler) = 0;

    virtual void start(uint32_t msec, bool repeat) = 0;
    virtual void restart() = 0;
    virtual void stop() = 0;

    virtual void setInterval(uint32_t msec) = 0;
    virtual uint32_t getInterval() const = 0;

    virtual void setRepeat(bool repeat) = 0;
    virtual bool getRepeat() const = 0;

    virtual bool isRunning() const = 0;
};

class MainLoop;

class Timer : public ITimer
{
public:
    static constexpr uint32_t Infinite = static_cast<uint32_t>(-1);

public:
    ~Timer() override;

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void setHandler(ITimerHandler* handler) override;

    void start(uint32_t msec, bool repeat) override;
    void restart() override;
    void stop() override;

    void setInterval(uint32_t msec) override;
    uint32_t getInterval() const override;

    void setRepeat(bool repeat) override;
    bool getRepeat() const override;

    bool isRunning() const override;

private:
    friend class MainLoop;

    Timer(MainLoop& loop);

    enum class State
    {
        Stopped,
        Queued,
        Executing,
        RequeuePending
    };

private:
    /*
     * MainLoop-only methods.
     * Hidden from ITimer users.
     */
    uint64_t getExpiryFromLoop() const;

    bool tryBeginExecuteFromLoop();
    bool executeFromLoop();
    void requeueFromLoop();

private:
    void stopAndWait();

    static uint64_t makeExpiry(uint32_t interval);

private:
    MainLoop& mLoop;
    /*
     * Serializes public control APIs:
     * start(), restart(), stop(), stopAndWait(), destructor.
     */
    Mutex mControlLock;

    /*
     * State is atomic because MainLoop transitions Queued -> Executing
     * while holding only MainLoop's timer-list lock.
     */
    std::atomic<State>    mState;
    std::atomic<bool>     mStopRequested;
    std::atomic<uint64_t> mExpiry;

    /*
     * Used for waiting until MainLoop no longer touches this Timer.
     */
    mutable Mutex mWaitLock;
    CondVar mStateChanged;

    /*
     * Handler and configuration.
     */
    mutable Mutex mConfigLock;

    ITimerHandler* mHandler;
    uint32_t       mInterval;
    bool           mRepeat;
};
