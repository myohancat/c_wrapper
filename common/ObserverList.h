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
#include "CondVar.h"
#include <stddef.h>

#include <array>
#include <bitset>
#include <utility>

template <typename T, size_t MaxObservers>
class RawObserverList
{
public:
    enum class RemoveResult
    {
        Removed,
        NotFound,
        NullObserver
    };

    RawObserverList() = default;
    ~RawObserverList() = default;

    RawObserverList(const RawObserverList&) = delete;
    RawObserverList& operator=(const RawObserverList&) = delete;
    RawObserverList(RawObserverList&&) = delete;
    RawObserverList& operator=(RawObserverList&&) = delete;

    bool add(T* observer)
    {
        if (observer == nullptr)
            return false;

        Lock<Mutex> lock(mLock);

        size_t freeIndex = MaxObservers;

        for (size_t i = 0; i < MaxObservers; ++i)
        {
            if (!mOccupiedBits.test(i))
            {
                if (freeIndex == MaxObservers)
                    freeIndex = i;

                continue;
            }

            const Entry& entry = mEntries[i];

            if (entry.observer == observer)
                return false;
        }

        if (freeIndex == MaxObservers)
            return false;

        ++mGeneration;

        Entry& target = mEntries[freeIndex];
        target.observer = observer;
        target.activeCalls = 0;
        target.generation = mGeneration;
        target.removed = false;
        target.waitingRemoval = false;

        mOccupiedBits.set(freeIndex);

        return true;
    }

    bool add(T& observer)
    {
        return add(&observer);
    }

    RemoveResult remove(T* observer)
    {
        if (observer == nullptr)
            return RemoveResult::NullObserver;

        Lock<Mutex> lock(mLock);

        const size_t index = findActiveEntryIndexLocked(observer);
        if (index == MaxObservers)
            return RemoveResult::NotFound;

        Entry& entry = mEntries[index];
        entry.removed = true;

        if (entry.activeCalls == 0 && !entry.waitingRemoval)
            resetEntryLocked(index);

        return RemoveResult::Removed;
    }

    RemoveResult remove(T& observer)
    {
        return remove(&observer);
    }

    // IMPORTANT:
    // Do not use this function in callback.
    // It's cause deadlock. Please use remove in callback
    RemoveResult removeAndWait(T* observer)
    {
        if (observer == nullptr)
            return RemoveResult::NullObserver;

        Lock<Mutex> lock(mLock);

        const size_t index = findActiveEntryIndexLocked(observer);
        if (index == MaxObservers)
            return RemoveResult::NotFound;

        Entry& entry = mEntries[index];
        entry.removed = true;

        if (entry.activeCalls == 0)
        {
            resetEntryLocked(index);
            return RemoveResult::Removed;
        }

        entry.waitingRemoval = true;

        mChanged.wait(lock, [&entry]() {
            return entry.activeCalls == 0;
        });

        resetEntryLocked(index);
        return RemoveResult::Removed;
    }

    RemoveResult removeAndWait(T& observer)
    {
        return removeAndWait(&observer);
    }

    template <typename Func>
    void notify(Func&& action) noexcept
    {
        static_assert(
                noexcept(std::declval<Func&>()(std::declval<T*>())),
                "RawObserverList::notify() callback must be noexcept"
        );

        size_t startGeneration = 0;
        std::bitset<MaxObservers> occupiedSnapshot;

        {
            Lock<Mutex> lock(mLock);
            startGeneration = mGeneration;
            occupiedSnapshot = mOccupiedBits;
        }

        for (size_t i = 0; i < MaxObservers; ++i)
        {
            if (!occupiedSnapshot.test(i))
                continue;

            T* target = acquireForNotify(i, startGeneration);
            if (target == nullptr)
                continue;

            action(target);

            releaseAfterNotify(i);
        }

        releaseRemoved();
    }

    bool contains(T* observer) const
    {
        if (observer == nullptr)
            return false;

        Lock<Mutex> lock(mLock);

        for (size_t i = 0; i < MaxObservers; ++i)
        {
            if (!mOccupiedBits.test(i))
                continue;

            const Entry& entry = mEntries[i];

            if (entry.observer == observer && !entry.removed)
                return true;
        }

        return false;
    }

    bool contains(T& observer) const
    {
        return contains(&observer);
    }

private:
    struct Entry
    {
        T* observer = nullptr;
        size_t activeCalls = 0;
        size_t generation = 0;
        bool removed = false;
        bool waitingRemoval = false;
    };

    size_t findActiveEntryIndexLocked(T* observer) const
    {
        for (size_t i = 0; i < MaxObservers; ++i)
        {
            if (!mOccupiedBits.test(i))
                continue;

            const Entry& entry = mEntries[i];

            if (entry.observer == observer && !entry.removed)
                return i;
        }

        return MaxObservers;
    }

    T* acquireForNotify(size_t index, size_t startGeneration)
    {
        Lock<Mutex> lock(mLock);

        if (!mOccupiedBits.test(index))
            return nullptr;

        Entry& entry = mEntries[index];

        if (entry.removed)
            return nullptr;

        if (entry.generation > startGeneration)
            return nullptr;

        ++entry.activeCalls;
        return entry.observer;
    }

    void releaseAfterNotify(size_t index)
    {
        bool notifyWaiter = false;

        {
            Lock<Mutex> lock(mLock);

            if (!mOccupiedBits.test(index))
                return;

            Entry& entry = mEntries[index];

            if (entry.activeCalls > 0)
                --entry.activeCalls;

            if (entry.activeCalls == 0)
            {
                notifyWaiter = entry.waitingRemoval;

                if (entry.removed && !entry.waitingRemoval)
                    resetEntryLocked(index);
            }
        }

        if (notifyWaiter)
            mChanged.broadcast();
    }

    void releaseRemoved()
    {
        Lock<Mutex> lock(mLock);

        for (size_t i = 0; i < MaxObservers; ++i)
        {
            if (!mOccupiedBits.test(i))
                continue;

            Entry& entry = mEntries[i];

            if (entry.removed && !entry.waitingRemoval && entry.activeCalls == 0)
                resetEntryLocked(i);
        }
    }

    void resetEntryLocked(size_t index)
    {
        Entry& entry = mEntries[index];

        entry.observer = nullptr;
        entry.activeCalls = 0;
        entry.generation = 0;
        entry.removed = false;
        entry.waitingRemoval = false;

        mOccupiedBits.reset(index);
    }

private:
    mutable Mutex mLock;
    CondVar mChanged;

    std::array<Entry, MaxObservers> mEntries{};
    std::bitset<MaxObservers> mOccupiedBits{};
    size_t mGeneration = 0;
};
