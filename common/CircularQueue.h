/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include <stddef.h>

#include <array>
#include <utility>

/**
 * Primitive Circular Queue, But not thread-safe.
 */
template <typename T, size_t capacity>
class CircularQueue
{
    static_assert(capacity > 0, "Queue capacity must be greater than 0");

public:
    CircularQueue() = default;
    virtual ~CircularQueue() = default;

    CircularQueue(const CircularQueue&) = delete;
    CircularQueue& operator=(const CircularQueue&) = delete;

    bool put(T t) noexcept
    {
        if (isFull())
            return false;

        mBuffer[mRear] = std::move(t);
        mRear = (mRear + 1) % kCapacity;
        ++mSize;

        return true;
    }

    bool get(T* t) noexcept
    {
        if (isEmpty())
            return false;

        if (t)
            *t = std::move(mBuffer[mFront]);

        mFront = (mFront + 1) % kCapacity;
        --mSize;

        return true;
    }

    bool isFull() const  { return mSize >= kCapacity; }
    bool isEmpty() const { return mSize == 0; }

private:
    static constexpr size_t kCapacity = capacity;

    std::array<T, kCapacity> mBuffer;
    size_t mSize  = 0;
    size_t mFront = 0;
    size_t mRear  = 0;
};
