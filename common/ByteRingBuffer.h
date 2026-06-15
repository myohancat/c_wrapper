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

#include <string.h>
#include <stdint.h>
#include <array>
#include <algorithm>

template <size_t Capacity>
class ByteRingBuffer
{
    static_assert(Capacity > 0, "Ring Buffer capacity must be greater than 0");

public:
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    static constexpr bool SYSTEM_BIG_ENDIAN = false;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    static constexpr bool SYSTEM_BIG_ENDIAN = true;
#else
#   error "Unknown byte order"
#endif

    ByteRingBuffer() : mSize(0), mFront(0), mRear(0) { }
    ~ByteRingBuffer() { }

    ByteRingBuffer(const ByteRingBuffer&) = delete;
    ByteRingBuffer& operator=(const ByteRingBuffer&) = delete;

    size_t write(const uint8_t* buf, size_t len)
    {
        if (buf == nullptr || len == 0)
            return 0;

        Lock<Mutex> lock(mLock);

        size_t can_write = std::min(len, Capacity - mSize);
        if (can_write == 0) return 0;

        size_t first_part = std::min(can_write, Capacity - mRear);
        memcpy(mData.data() + mRear, buf, first_part);

        if (can_write > first_part)
        {
            memcpy(mData.data(), buf + first_part, can_write - first_part);
        }

        mRear = (mRear + can_write) % Capacity;
        mSize += can_write;

        return can_write;
    }

    size_t read(uint8_t* buf, size_t len)
    {
        if (buf == nullptr || len == 0)
            return 0;

        Lock<Mutex> lock(mLock);

        size_t bytesRead = _peek(0, buf, len);
        if (bytesRead > 0)
            _drop(bytesRead);

        return bytesRead;
    }

    size_t peek(uint8_t* buf, size_t len)
    {
        if (buf == nullptr || len == 0)
            return 0;

        Lock<Mutex> lock(mLock);
        return _peek(0, buf, len);
    }

    bool drop(size_t size)
    {
        Lock<Mutex> lock(mLock);
        return _drop(size);
    }

    bool read8(uint8_t* val);
    bool read16(uint16_t* val, bool bigEndian = SYSTEM_BIG_ENDIAN);
    bool read32(uint32_t* val, bool bigEndian = SYSTEM_BIG_ENDIAN);

    bool peek8(size_t offset, uint8_t* val);
    bool peek16(size_t offset, uint16_t* val, bool bigEndian = SYSTEM_BIG_ENDIAN);
    bool peek32(size_t offset, uint32_t* val, bool bigEndian = SYSTEM_BIG_ENDIAN);

    size_t capacity() const;
    size_t size() const;
    size_t available() const;

private:
    size_t _peek(size_t offset, uint8_t* buf, size_t len)
    {
        if (offset >= mSize || len == 0)
            return 0;

        size_t can_read = std::min(len, mSize - offset);
        size_t start_idx = (mFront + offset) % Capacity;

        size_t first_part = std::min(can_read, Capacity - start_idx);
        if (buf)
        {
            memcpy(buf, mData.data() + start_idx, first_part);
            if (can_read > first_part)
            {
                memcpy(buf + first_part, mData.data(), can_read - first_part);
            }
        }

        return can_read;
    }

    bool _drop(size_t size)
    {
        if (mSize < size)
            return false;

        mFront = (mFront + size) % Capacity;
        mSize -= size;

        return true;
    }

    template <typename T>
    bool _peekType(size_t offset, T* val, bool bigEndian)
    {
        uint8_t temp[sizeof(T)];

        if (val == nullptr)
            return false;

        if (_peek(offset, temp, sizeof(T)) != sizeof(T))
            return false;

        *val = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            if (bigEndian)
                *val |= (static_cast<T>(temp[i]) << (8 * (sizeof(T) - 1 - i)));
            else
                *val |= (static_cast<T>(temp[i]) << (8 * i));
        }

        return true;
    }

    template <typename T>
    bool _readType(T* val, bool bigEndian)
    {
        if (_peekType(0, val, bigEndian))
        {
            _drop(sizeof(T));
            return true;
        }
        return false;
    }

private:
    mutable Mutex mLock;

    std::array<uint8_t, Capacity> mData{};
    size_t mSize;
    size_t mFront;
    size_t mRear;
};

template <size_t Capacity>
inline size_t ByteRingBuffer<Capacity>::capacity() const
{
    return Capacity;
}

template <size_t Capacity>
inline size_t ByteRingBuffer<Capacity>::size() const
{
    Lock<Mutex> lock(mLock);
    return mSize;
}

template <size_t Capacity>
inline size_t ByteRingBuffer<Capacity>::available() const
{
    Lock<Mutex> lock(mLock);
    return Capacity - mSize;
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::read8(uint8_t* val)
{
    Lock<Mutex> lock(mLock);
    return _readType<uint8_t>(val, false);
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::read16(uint16_t* val, bool bigEndian)
{
    Lock<Mutex> lock(mLock);
    return _readType<uint16_t>(val, bigEndian);
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::read32(uint32_t* val, bool bigEndian)
{
    Lock<Mutex> lock(mLock);
    return _readType<uint32_t>(val, bigEndian);
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::peek8(size_t offset, uint8_t* val)
{
    Lock<Mutex> lock(mLock);
    return _peekType<uint8_t>(offset, val, false);
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::peek16(size_t offset, uint16_t* val, bool bigEndian)
{
    Lock<Mutex> lock(mLock);
    return _peekType<uint16_t>(offset, val, bigEndian);
}

template <size_t Capacity>
inline bool ByteRingBuffer<Capacity>::peek32(size_t offset, uint32_t* val, bool bigEndian)
{
    Lock<Mutex> lock(mLock);
    return _peekType<uint32_t>(offset, val, bigEndian);
}
