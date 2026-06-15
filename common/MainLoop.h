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
#include "ObserverList.h"
#include "CircularQueue.h"
#include "FixedFunction.h"

#include "Mutex.h"
#include "Log.h"

#include <atomic>
#include <list>

class IFdWatcher
{
public:
    virtual ~IFdWatcher() = default;

    virtual int  getFD() = 0;
    virtual bool onFdReadable(int fd) = 0;
};

class MainLoop
{
public:
    MainLoop();
    ~MainLoop();

    Timer createTimer();

    void addFdWatcher(IFdWatcher* watcher);
    void removeFdWatcher(IFdWatcher* watcher);

    template <typename Func>
    void post(Func&& callback)
    {
        Lock<Mutex> lock(mFunctionLock);

        if (mFunctions.put(FixedFunction(std::forward<Func>(callback))))
        {
            wakeup();
        }
        else
            LOGE("MainLoop task queue is full!");
    }

    void loop();
    void wakeup();
    void terminate();

private:
    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;

    bool loopOnce();
    friend class Timer;

    void addTimer(Timer* timer);
    void removeTimer(Timer* timer);

    uint32_t runTimers();
    bool     runFunctions();

    enum class LoopCommand : uint8_t
    {
        Wakeup    = 'S',
        Terminate = 'T'
    };

    void sendCommand(LoopCommand command);
    bool drainCommandPipe(bool& terminated);

    void insertTimerLocked(Timer* timer);
    Timer* takeExpiredTimerLocked(uint64_t now);

    uint32_t getNextTimerTimeoutLocked(uint64_t now) const;
    uint32_t getNextTimerTimeout();

private:
    static constexpr uint32_t WaitTimeMs = 10 * 1000;

    static constexpr std::size_t kMaxFdWatcherSize = 64;
    static constexpr std::size_t kMaxFunctionSize  = 128;
    static constexpr std::size_t kMaxTimerSize     = 256;

    RawObserverList<IFdWatcher, kMaxFdWatcherSize> mFdWatchers;

    Mutex mFunctionLock;
    CircularQueue<FixedFunction, kMaxFunctionSize> mFunctions;

    // TODO. Change this.
    // Timer createTimer(); // add
    // Pool<Timer, kMaxTimerSize> mTimerPool;
    // SortedLinkedList<Timer, kMaxTimerSize> mTimers;
    using TimerList = std::list<Timer*>;
    Mutex mTimerLock;
    TimerList mTimers;

    int mEpollFd;
    int mPipe[2];

    std::atomic<bool> mTerminated;
};
