/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "MainLoop.h"
#include <signal.h>

#include <utility>

class Platform
{
public:
    using InitCallback = bool (*)(void);
    using DeinitCallback = void (*)(void);

private:
    inline static bool sInstantiated = false;
    inline static MainLoop* sLoop = nullptr;

    static void _sig_handler(int) noexcept
    {
        if (sLoop) {
            sLoop->terminate();
        }
    }

    bool mSignalInstalled = false;
    struct sigaction mOldInt {};
    struct sigaction mOldTerm {};

    DeinitCallback mDeinitCallback = nullptr;

    bool installSignal() noexcept
    {
        struct sigaction sa {};
        sa.sa_handler = _sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGINT, &sa, &mOldInt) != 0)
            return false;

        if (sigaction(SIGTERM, &sa, &mOldTerm) != 0)
        {
            (void)sigaction(SIGINT, &mOldInt, nullptr);
            return false;
        }

        return true;
    }

    void removeSignal() noexcept
    {
        (void)sigaction(SIGINT, &mOldInt, nullptr);
        (void)sigaction(SIGTERM, &mOldTerm, nullptr);
    }

public:
    explicit Platform(MainLoop& loop) noexcept
    {
        ASSERT_IF(sInstantiated, "Platform instance already exists! You can only create one.");

        sLoop = &loop;
        sInstantiated = true;
    }

    ~Platform() noexcept
    {
        deinit();

        sLoop = nullptr;
        sInstantiated = false;
    }

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;

    bool init(InitCallback initCb = nullptr, DeinitCallback deinitCb = nullptr) noexcept
    {
        if (mSignalInstalled)
            return true;

        if (!installSignal())
            return false;

        mSignalInstalled = true;

        if (initCb)
        {
            if (!initCb())
            {
                removeSignal();
                mSignalInstalled = false;
                return false;
            }
        }

        mDeinitCallback = std::move(deinitCb);

        return true;
    }

    void deinit() noexcept
    {
        if (!mSignalInstalled)
            return;

        removeSignal();
        mSignalInstalled = false;

        if (mDeinitCallback)
        {
            mDeinitCallback();
            mDeinitCallback = nullptr;
        }
    }
};


