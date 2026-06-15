/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#include "MainLoop.h"

#include "SysTime.h"
#include "Log.h"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <sys/epoll.h>

#ifndef SAFE_CLOSE
#define SAFE_CLOSE(fd)      \
    do                      \
    {                       \
        if ((fd) >= 0)      \
        {                   \
            close(fd);      \
            (fd) = -1;      \
        }                   \
    } while (0)
#endif

namespace
{
bool setNonBlockAndCloseOnExec(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return false;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0)
        return false;

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return false;

    return true;
}
}

MainLoop::MainLoop()
    : mEpollFd(-1)
    , mPipe { -1, -1 }
    , mTerminated(false)
{
    mEpollFd = epoll_create1(EPOLL_CLOEXEC);
    if (mEpollFd < 0)
    {
        LOGE("cannot create epoll fd! errno=%d", errno);
        return;
    }

#if defined(__linux__)
    if (pipe2(mPipe, O_NONBLOCK | O_CLOEXEC) < 0)
#else
    if (pipe(mPipe) < 0)
#endif
    {
        LOGE("cannot create command pipe! errno=%d", errno);
        return;
    }

#if !defined(__linux__)
    if (!setNonBlockAndCloseOnExec(mPipe[0]) ||
        !setNonBlockAndCloseOnExec(mPipe[1]))
    {
        LOGE("cannot configure command pipe! errno=%d", errno);
        return;
    }
#else
    /*
     * pipe2() already configured both ends.
     * Keep this helper referenced for non-Linux builds only.
     */
    (void)setNonBlockAndCloseOnExec;
#endif

    struct epoll_event event;
    memset(&event, 0, sizeof(event));

    event.events = EPOLLIN;
    event.data.ptr = nullptr; // nullptr means command pipe.

    if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mPipe[0], &event) < 0)
    {
        LOGE("epoll_ctl add command pipe failed! errno=%d", errno);
    }
}

MainLoop::~MainLoop()
{
    SAFE_CLOSE(mPipe[0]);
    SAFE_CLOSE(mPipe[1]);
    SAFE_CLOSE(mEpollFd);
}

Timer MainLoop::createTimer()
{
    return Timer(*this);
}

void MainLoop::addFdWatcher(IFdWatcher* watcher)
{
    if (!watcher)
        return;

    if (mEpollFd < 0)
    {
        LOGE("epoll fd is invalid!");
        return;
    }

    const int fd = watcher->getFD();
    if (fd < 0)
    {
        LOGE("invalid fd watcher. fd=%d", fd);
        return;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(event));

    event.events = EPOLLIN;
    event.data.ptr = watcher;

    if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        if (errno == EEXIST)
            LOGE("fd watcher already exists in epoll. fd=%d", fd);
        else
            LOGE("epoll_ctl ADD failed. fd=%d errno=%d", fd, errno);

        return;
    }

    mFdWatchers.add(watcher);
}

void MainLoop::removeFdWatcher(IFdWatcher* watcher)
{
    if (!watcher)
        return;

    const int fd = watcher->getFD();

    if (mEpollFd >= 0 && fd >= 0)
    {
        if (epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, nullptr) < 0)
        {
            if (errno != ENOENT && errno != EBADF)
            {
                LOGE("epoll_ctl DEL failed. fd=%d errno=%d", fd, errno);
            }
        }
    }

    mFdWatchers.remove(watcher);
}

void MainLoop::insertTimerLocked(Timer* timer)
{
    if (!timer)
        return;

    TimerList::iterator exists =
        std::find(mTimers.begin(), mTimers.end(), timer);

    if (exists != mTimers.end())
    {
        LOGE("timer already exists in MainLoop");
        return;
    }

    TimerList::iterator pos =
        std::find_if(
            mTimers.begin(),
            mTimers.end(),
            [timer](const Timer* other) {
                return timer->getExpiryFromLoop() < other->getExpiryFromLoop();
            }
        );

    mTimers.insert(pos, timer);
}

void MainLoop::addTimer(Timer* timer)
{
    if (!timer)
        return;

    {
        Lock<Mutex> lock(mTimerLock);
        insertTimerLocked(timer);
    }

    wakeup();
}

void MainLoop::removeTimer(Timer* timer)
{
    if (!timer)
        return;

    {
        Lock<Mutex> lock(mTimerLock);

        for (TimerList::iterator it = mTimers.begin(); it != mTimers.end(); ++it)
        {
            if (*it == timer)
            {
                mTimers.erase(it);
                break;
            }
        }
    }

    wakeup();
}

Timer* MainLoop::takeExpiredTimerLocked(uint64_t now)
{
    if (mTimers.empty())
        return nullptr;

    Timer* timer = mTimers.front();

    if (!timer)
    {
        mTimers.pop_front();
        return nullptr;
    }

    const uint64_t expiry = timer->getExpiryFromLoop();

    if (expiry > now)
        return nullptr;

    /*
     * Transition Queued -> Executing while the timer is still in the list.
     *
     * This prevents Timer::~Timer()/stopAndWait() from believing MainLoop
     * no longer holds the execution path.
     */
    const bool canExecute = timer->tryBeginExecuteFromLoop();

    /*
     * Remove before callback.
     * MainLoop must not hold mTimerLock while executing user code.
     */
    mTimers.pop_front();

    if (!canExecute)
        return nullptr;

    return timer;
}

uint32_t MainLoop::getNextTimerTimeoutLocked(uint64_t now) const
{
    if (mTimers.empty())
        return WaitTimeMs;

    Timer* timer = mTimers.front();

    if (!timer)
        return 0;

    const uint64_t expiry = timer->getExpiryFromLoop();

    if (expiry <= now)
        return 0;

    const uint64_t diff = expiry - now;

    if (diff > static_cast<uint64_t>(WaitTimeMs))
        return WaitTimeMs;

    return static_cast<uint32_t>(diff);
}

uint32_t MainLoop::getNextTimerTimeout()
{
    Lock<Mutex> lock(mTimerLock);
    return getNextTimerTimeoutLocked(SysTime::getTickCountMs());
}

uint32_t MainLoop::runTimers()
{
    Timer* expiredTimer = nullptr;

    {
        Lock<Mutex> lock(mTimerLock);
        expiredTimer = takeExpiredTimerLocked(SysTime::getTickCountMs());
    }

    if (!expiredTimer)
    {
        return getNextTimerTimeout();
    }

    const bool keepTimer = expiredTimer->executeFromLoop();

    if (keepTimer)
    {
        expiredTimer->requeueFromLoop();
    }

    return getNextTimerTimeout();
}

void MainLoop::loop()
{
    while(loopOnce()) { /* NOP */ }
}

bool MainLoop::loopOnce()
{
    if (mEpollFd < 0 || mPipe[0] < 0)
        return false;

    while (runFunctions())
    {
    }

    uint32_t timeToWait = 0;
    while ((timeToWait = runTimers()) == 0)
    {
    }

    static constexpr int MaxEvents = 32;
    struct epoll_event events[MaxEvents];

    const int eventCount = epoll_wait(
        mEpollFd,
        events,
        MaxEvents,
        static_cast<int>(timeToWait)
    );

    if (eventCount == 0)
        return true;

    if (eventCount < 0)
    {
        if (errno == EINTR)
            return true;

        LOGE("epoll_wait error occurred! errno=%d", errno);
        return false;
    }

    for (int i = 0; i < eventCount; ++i)
    {
        if (events[i].data.ptr == nullptr)
        {
            bool terminated = false;

            if (!drainCommandPipe(terminated))
                return true;

            if (terminated || mTerminated.load(std::memory_order_acquire))
            {
                LOGI(">> terminated received.");
                return false;
            }

            continue;
        }

        IFdWatcher* watcher = static_cast<IFdWatcher*>(events[i].data.ptr);
        if (!watcher)
            continue;

        const int fd = watcher->getFD();
        if (fd < 0)
            continue;

        if (events[i].events & (EPOLLERR | EPOLLHUP))
        {
            LOGE("epoll fd error. fd=%d events=%u", fd, events[i].events);
        }

        if (events[i].events & EPOLLIN)
        {
            watcher->onFdReadable(fd);
        }
    }

    return true;
}

bool MainLoop::runFunctions()
{
    bool executedAny = false;
    FixedFunction func;

    while (true)
    {
        FixedFunction func;
        {
            Lock<Mutex> lock(mFunctionLock);
            if (!mFunctions.get(&func))
                break;
        }

        if (func)
            func();

        executedAny = true;
    }

    return executedAny;
}

void MainLoop::sendCommand(LoopCommand command)
{
    if (mPipe[1] < 0)
        return;

    const uint8_t value = static_cast<uint8_t>(command);

    while (true)
    {
        const ssize_t ret = write(mPipe[1], &value, sizeof(value));

        if (ret == static_cast<ssize_t>(sizeof(value)))
            return;

        if (ret < 0 && errno == EINTR)
            continue;

        /*
         * Pipe full means MainLoop already has pending commands/wakeup bytes.
         */
        if (ret < 0 && errno == EAGAIN)
            return;

        if (ret < 0)
            LOGE("command pipe write failed! errno=%d", errno);

        return;
    }
}

bool MainLoop::drainCommandPipe(bool& terminated)
{
    terminated = false;

    if (mPipe[0] < 0)
        return false;

    while (true)
    {
        uint8_t value = 0;
        const ssize_t ret = read(mPipe[0], &value, sizeof(value));

        if (ret == static_cast<ssize_t>(sizeof(value)))
        {
            const LoopCommand command = static_cast<LoopCommand>(value);

            switch (command)
            {
                case LoopCommand::Wakeup:
                    break;

                case LoopCommand::Terminate:
                    terminated = true;
                    break;

                default:
                    LOGE("unknown loop command. value=%u", value);
                    break;
            }

            continue;
        }

        if (ret < 0 && errno == EINTR)
            continue;

        if (ret < 0 && errno == EAGAIN)
            return true;

        if (ret == 0)
            return true;

        LOGE("command pipe read failed! errno=%d", errno);
        return false;
    }
}

void MainLoop::wakeup()
{
    sendCommand(LoopCommand::Wakeup);
}

void MainLoop::terminate()
{
    /*
     * Atomic flag protects termination even if command pipe is full.
     */
    mTerminated.store(true, std::memory_order_release);
    sendCommand(LoopCommand::Terminate);
}
