/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include "CircularQueue.h"
#include "Mutex.h"
#include "CondVar.h"

#include <utility>

template<typename T, size_t Capacity>
class SyncQueue
{
public:
    static constexpr int INFINITE = -1;

    static_assert(Capacity > 0, "Capacity must be greater than zero");

    SyncQueue() : mEOS(false) { }
    virtual ~SyncQueue() { }

    SyncQueue(const SyncQueue&) = delete;
    SyncQueue& operator=(const SyncQueue&) = delete;

    bool put(const T t, int timeoutMs = INFINITE)
    {
        Lock<Mutex> lock(mLock);

        while (mQueue.isFull() && !mEOS)
        {
            if (timeoutMs == INFINITE)
                mCvFull.wait(mLock);
            else if (timeoutMs == 0)
                return false;
            else
            {
                if (!mCvFull.wait(mLock, timeoutMs))
                    return false;
            }
        }

        if (mQueue.isFull())
            return false;

        if (mEOS)
            return false;

        mQueue.put(t);
        mCvEmpty.signal();

        return true;
    }

    bool putForce(T t)
    {
        bool needDispose = false;
        T oldData;

        {
            Lock<Mutex> lock(mLock);

            if (mEOS) return false;

            if (mQueue.isFull())
            {
                mQueue.get(&oldData);
                needDispose = true;
            }

            mQueue.put(t);
            mCvEmpty.signal();
        }

        if (needDispose)
            dispose(std::move(oldData));
    }

    bool get(T* t, int timeoutMs = INFINITE)
    {
        Lock<Mutex> lock(mLock);

        while (mQueue.isEmpty() && !mEOS)
        {
            if (timeoutMs == INFINITE)
                mCvEmpty.wait(mLock);
            else if (timeoutMs == 0)
                return false;
            else
            {
                if (!mCvEmpty.wait(mLock, timeoutMs))
                    return false;
            }
        }

        if (mQueue.isEmpty())
            return false;

        if (mEOS)
            return false;

        mQueue.get(t);
        mCvFull.signal();

        return true;
    }

    void flush()
    {
        while (true)
        {
            T oldValue;
            {
                Lock<Mutex> lock(mLock);
                if (mQueue.isEmpty())
                    break;

                mQueue.get(&oldValue);
            }
            dispose(std::move(oldValue));
        }

        mCvFull.broadcast();
    }

    void setEOS(bool eos)
    {
        {
            Lock<Mutex> lock(mLock);
            mEOS = eos;
        }

        mCvFull.broadcast();
        mCvEmpty.broadcast();
    }

    bool isEOS() const   { Lock<Mutex> lock(mLock); return mEOS; }
    bool isEmpty() const { Lock<Mutex> lock(mLock); return mQueue.isEmpty(); }
    bool isFull() const  { Lock<Mutex> lock(mLock); return mQueue.isFull(); }

protected:
    /* MUST OVERRIDE to free T */
    virtual void dispose(T d) { (void)d; }

private:
    CircularQueue<T, Capacity> mQueue;

    mutable Mutex mLock;
    bool          mEOS;
    CondVar       mCvFull;
    CondVar       mCvEmpty;
};
