/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

struct RealTime
{
    uint16_t mYear;
    uint8_t  mMonth;
    uint8_t  mDay;
    uint8_t  mHour;
    uint8_t  mMin;
    uint8_t  mSec;
    uint16_t mMsec;
};

class SysTime
{
public:
    static uint64_t getTickCountMs()
    {
        struct timespec now {};
        clock_gettime(CLOCK_MONOTONIC, &now);

        return static_cast<uint64_t>(now.tv_sec) * 1000
             + now.tv_nsec / 1000000;
    }

    static uint64_t getTickCountUs()
    {
        struct timespec now {};
        clock_gettime(CLOCK_MONOTONIC, &now);

        return static_cast<uint64_t>(now.tv_sec) * 1000000
             + now.tv_nsec / 1000;
    }

    static uint64_t getCurrentTime()
    {
        struct timeval tv {};
        gettimeofday(&tv, nullptr);

        return static_cast<uint64_t>(tv.tv_sec) * 1000
             + tv.tv_usec / 1000;
    }

    static void getCurrentTime(RealTime& time)
    {
        struct timeval tv {};
        gettimeofday(&tv, nullptr);

        time_t current = tv.tv_sec;

        struct tm result {};
        localtime_r(&current, &result);

        time.mYear  = result.tm_year + 1900;
        time.mMonth = result.tm_mon + 1;
        time.mDay   = result.tm_mday;
        time.mHour  = result.tm_hour;
        time.mMin   = result.tm_min;
        time.mSec   = result.tm_sec;
        time.mMsec  = tv.tv_usec / 1000;
    }
};
