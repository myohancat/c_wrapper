/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "Log.h"
#include "Mutex.h"

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include <array>
#include <new>
#include <type_traits>
#include <utility>

/**
 * Primitive Pool, But not thread-safe.
 */
template <typename T, size_t Capacity>
class Pool
{
    static_assert(Capacity > 0, "Pool capacity must be greater than 0");

    static_assert(!std::is_const<T>::value, "T must not be const");
    static_assert(!std::is_volatile<T>::value, "T must not be volatile");

private:
    using BitmapChunk = uint32_t;

    static constexpr size_t INVALID_INDEX = Capacity;
    static constexpr size_t STORAGE_SIZE = Capacity * sizeof(T);

    static constexpr size_t BITS_PER_CHUNK = sizeof(BitmapChunk) * CHAR_BIT;
    static constexpr size_t BITMAP_SIZE = 1 + ((Capacity - 1) / BITS_PER_CHUNK);

    std::array<BitmapChunk, BITMAP_SIZE> mUsedMap{};

    std::array<size_t, Capacity> mNext{};

    size_t mFreeHead;

    alignas(T) std::array<unsigned char, STORAGE_SIZE> mStorage;

public:
    Pool() : mFreeHead(0)
    {
        for (size_t i = 0; i + 1 < Capacity; ++i)
        {
            mNext[i] = i + 1;
        }

        mNext[Capacity - 1] = INVALID_INDEX;
    }

    ~Pool() = default;

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    T* alloc() noexcept
    {
        if (mFreeHead == INVALID_INDEX)
        {
            LOGE("Pool is completely exhausted!");
            return nullptr;
        }

        const size_t index = mFreeHead;
        mFreeHead = mNext[index];

        setUsed(index);

        return reinterpret_cast<T*>(&mStorage[index * sizeof(T)]);
    }

    bool free(T* ptr) noexcept
    {
        if (ptr == nullptr)
        {
            return true;
        }

        const size_t index = getIndex(ptr);

        if (index == INVALID_INDEX)
        {
            return false;
        }

        if (!isUsed(index))
        {
            return false;
        }

        setFree(index);

        mNext[index] = mFreeHead;
        mFreeHead = index;

        return true;
    }

    template <typename Cleanup>
    void clear(Cleanup&& cleanup) noexcept
    {
        static_assert(std::is_nothrow_invocable<Cleanup&, T*>::value, "Pool cleanup callback must be noexcept");

        for (size_t i = 0; i < Capacity; ++i)
        {
            if (isUsed(i))
            {
                T* t = reinterpret_cast<T*>(&mStorage[i * sizeof(T)]);

                cleanup(t);
            }
        }

        reset();
    }

    void reset() noexcept
    {
        mUsedMap.fill(0);

        for (size_t i = 0; i + 1 < Capacity; ++i)
        {
            mNext[i] = i + 1;
        }

        mNext[Capacity - 1] = INVALID_INDEX;
        mFreeHead = 0;
    }

    bool isAllocated(const T* ptr) const noexcept
    {
        const size_t index = getIndex(ptr);

        return index != INVALID_INDEX && isUsed(index);
    }

    constexpr size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    static BitmapChunk bitMask(size_t index) noexcept
    {
        return static_cast<BitmapChunk>(1) << (index % BITS_PER_CHUNK);
    }

    void setUsed(size_t index) noexcept
    {
        mUsedMap[index / BITS_PER_CHUNK] |= bitMask(index);
    }

    void setFree(size_t index) noexcept
    {
        mUsedMap[index / BITS_PER_CHUNK] &= static_cast<BitmapChunk>(~bitMask(index));
    }

    bool isUsed(size_t index) const noexcept
    {
        return (mUsedMap[index / BITS_PER_CHUNK] & bitMask(index)) != 0;
    }

    size_t getIndex(const T* ptr) const noexcept
    {
        if (ptr == nullptr)
            return INVALID_INDEX;

        const uintptr_t raw = reinterpret_cast<uintptr_t>(ptr);

        const uintptr_t start = reinterpret_cast<uintptr_t>(mStorage.data());

        if (raw < start)
            return INVALID_INDEX;

        const uintptr_t offset = raw - start;

        if (offset >= STORAGE_SIZE)
            return INVALID_INDEX;

        if ((offset % sizeof(T)) != 0)
            return INVALID_INDEX;

        return static_cast<size_t>(offset / sizeof(T));
    }
};

/**
 * Pool for C structures and POD-like data.
 *
 * Usage:
 *   DataPool<Struct, 10> mPool;
 *   Struct * s = mPool.alloc();
 *   mPool.free(s);
 */
template <typename T, size_t Capacity>
class DataPool
{
    static_assert(std::is_trivial<T>::value, "DataPool T must be a trivial type");

    static_assert(std::is_standard_layout<T>::value, "DataPool T must be a standard-layout type");

public:
    DataPool() = default;

    ~DataPool() noexcept
    {
        clear();
    }

    DataPool(const DataPool&) = delete;
    DataPool& operator=(const DataPool&) = delete;

    DataPool(DataPool&&) = delete;
    DataPool& operator=(DataPool&&) = delete;

    T* alloc() noexcept
    {
        Lock<Mutex> lock(mLock);

        T* const storage = mPool.alloc();

        if (storage == nullptr)
        {
            return nullptr;
        }

        return ::new (static_cast<void*>(storage)) T;
    }

    void free(T* ptr) noexcept
    {
        Lock<Mutex> lock(mLock);

        if (ptr == nullptr)
        {
            return;
        }

        if (!mPool.isAllocated(ptr))
        {
            LOGW("DataPool: invalid pointer or double free.");
            return;
        }

        ptr->~T();

        mPool.free(ptr);
    }

    void clear() noexcept
    {
        Lock<Mutex> lock(mLock);

        mPool.clear([](T* ptr) noexcept {
            ptr->~T();
        });
    }

    constexpr size_t capacity() const noexcept
    {
        return mPool.capacity();
    }

private:
    Mutex mLock;
    Pool<T, Capacity> mPool;
};


/**
 * Pool for Object.
 *
 * Usage:
 *   ObjectPool<Class, 10> mPool;
 *   Class* obj = mPool.create(1);
 *   mPool.destroy(obj);
 */
template <typename T, size_t Capacity>
class ObjectPool
{
    static_assert(std::is_nothrow_destructible<T>::value, "ObjectPool T destructor must be noexcept");

public:
    ObjectPool() = default;

    ~ObjectPool() noexcept
    {
        clear();
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    template <typename... Args>
    T* create(Args&&... args) noexcept
    {
        static_assert(std::is_nothrow_constructible<T, Args...>::value, "ObjectPool constructor must be noexcept");

        Lock<Mutex> lock(mLock);

        T* const storage = mPool.alloc();

        if (storage == nullptr)
        {
            return nullptr;
        }

        return ::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
    }

    void destroy(T* ptr) noexcept
    {
        Lock<Mutex> lock(mLock);
        if (ptr == nullptr)
        {
            return;
        }

        if (!mPool.isAllocated(ptr))
        {
            LOGW("ObjectPool: invalid pointer or double delete.");
            return;
        }

        ptr->~T();

        mPool.free(ptr);
    }

    void clear() noexcept
    {
        Lock<Mutex> lock(mLock);

        mPool.clear([](T* ptr) noexcept {
            ptr->~T();
        });
    }

    constexpr size_t capacity() const noexcept
    {
        return mPool.capacity();
    }

private:
    Mutex mLock;
    Pool<T, Capacity> mPool;
};
