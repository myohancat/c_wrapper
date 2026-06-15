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

#include <functional>
#include <new>
#include <type_traits>
#include <utility>

class FixedFunction
{
private:
    static constexpr size_t kStorageSize = 32;
    static constexpr size_t kStorageAlign = alignof(void*);

    alignas(kStorageAlign) unsigned char mStorage[kStorageSize];

    void (*mFnInvoker)(void*) = nullptr;
    void (*mFnDestructor)(void*) noexcept = nullptr;
    void (*mFnMover)(void* dst, void* src) noexcept = nullptr;

public:
    FixedFunction() noexcept = default;

    template <
        typename Func,
        typename F = std::decay_t<Func>,
        std::enable_if_t<!std::is_same_v<F, FixedFunction>, int> = 0>
    explicit FixedFunction(Func&& func)
    {
        static_assert(sizeof(F) <= kStorageSize,
                      "Function capture size exceeds 32 bytes");

        static_assert(alignof(F) <= kStorageAlign,
                      "Function alignment exceeds storage alignment");

        static_assert(std::is_invocable_r_v<void, F&>,
                      "Function must be invocable as void()");

        static_assert(std::is_nothrow_move_constructible_v<F>,
                      "Function must be nothrow move constructible");

        static_assert(std::is_nothrow_destructible_v<F>,
                      "Function must be nothrow destructible");

        ::new (static_cast<void*>(mStorage)) F(std::forward<Func>(func));

        mFnInvoker = [](void* storage) {
            std::invoke(*reinterpret_cast<F*>(storage));
        };

        mFnDestructor = [](void* storage) noexcept {
            reinterpret_cast<F*>(storage)->~F();
        };

        mFnMover = [](void* dst, void* src) noexcept {
            ::new (dst) F(std::move(*reinterpret_cast<F*>(src)));
            reinterpret_cast<F*>(src)->~F();
        };
    }

    ~FixedFunction() noexcept
    {
        reset();
    }

    FixedFunction(FixedFunction&& other) noexcept
    {
        move_from(std::move(other));
    }

    FixedFunction& operator=(FixedFunction&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            move_from(std::move(other));
        }

        return *this;
    }

    FixedFunction(const FixedFunction&) = delete;
    FixedFunction& operator=(const FixedFunction&) = delete;

    void operator()()
    {
        invoke();
    }

    void invoke()
    {
        if (mFnInvoker)
        {
            mFnInvoker(mStorage);
        }
    }

    explicit operator bool() const noexcept
    {
        return mFnInvoker != nullptr;
    }

    void reset() noexcept
    {
        if (mFnDestructor)
        {
            mFnDestructor(mStorage);

            mFnInvoker = nullptr;
            mFnDestructor = nullptr;
            mFnMover = nullptr;
        }
    }

private:
    void move_from(FixedFunction&& other) noexcept
    {
        if (other.mFnMover)
        {
            mFnInvoker = other.mFnInvoker;
            mFnDestructor = other.mFnDestructor;
            mFnMover = other.mFnMover;

            other.mFnMover(mStorage, other.mStorage);

            other.mFnInvoker = nullptr;
            other.mFnDestructor = nullptr;
            other.mFnMover = nullptr;
        }
    }
};
