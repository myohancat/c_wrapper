/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "WorkerThread.h"
#include "FixedFunction.h"
#include "Mutex.h"
#include "Log.h"

#include <utility>
#include <type_traits>

/*
 * Usage:
 *
 * AsyncThread myWorker("BackgroundWorker");
 *
 * myWorker.start([](AsyncThread::Context& ctx) {
 *      while (ctx.shouldRun())
 *      {
 *          ctx.msleep(100);
 *      }
 * });
 *
 * myWorker.stop();
 *
 */
class AsyncThread final : public IWorker
{
public:
    class Context
    {
    private:
        WorkerThread& mEngine;

    public:
        explicit Context(WorkerThread& engine) noexcept
            : mEngine(engine)
        {
        }

        void msleep(int msec)
        {
            mEngine.msleep(msec);
        }

        bool shouldRun() const noexcept
        {
            return mEngine.shouldRun();
        }
    };

public:
    AsyncThread(const char* name = "AsyncThread", int priority = 0, int cpuid = -1)
        : mThreadEngine(name, priority, cpuid)
    {
    }

    ~AsyncThread() noexcept override
    {
        stop();
    }

    template <typename Func>
    bool start(Func&& callback)
    {
        using F = std::decay_t<Func>;

        static_assert(std::is_invocable_r_v<void, F&, Context&>,
                "Callback must be invocable as void(AsyncThread::Context&)");

        ASSERT_IF(mThreadEngine.isCurrentThread(), "A thread cannot start itself.");

        Lock<Mutex> lock(mLifecycleLock);

        if (mThreadEngine.shouldRun())
            return false;

        mThreadEngine.stop();

        mWorkerTask = FixedFunction([this, cb = std::forward<Func>(callback)]() mutable {
                Context ctx(mThreadEngine);
                cb(ctx);
                });

        if (!mThreadEngine.start(*this))
        {
            return false;
        }

        return true;
    }

    void stop() noexcept
    {
        ASSERT_IF(mThreadEngine.isCurrentThread(), "A thread cannot stop itself.");

        Lock<Mutex> lock(mLifecycleLock);

        mThreadEngine.stop();
    }

private:
    void run() noexcept override
    {
        if (mWorkerTask)
        {
            mWorkerTask();
        }
    }

private:
    bool onPreStart() noexcept override { return true; }
    void onPostStart() noexcept override {}
    void onPreStop() noexcept override {}
    void onPostStop() noexcept override {}

    AsyncThread(const AsyncThread&) = delete;
    AsyncThread& operator=(const AsyncThread&) = delete;

private:
    WorkerThread mThreadEngine;
    FixedFunction mWorkerTask;
    Mutex mLifecycleLock;
};
