#include <folly/Synchronized.h>
#include <iostream>
#include <vector>
#include <thread>

// compile: g++ -std=c++17 04_synchronized.cpp -o 04_synchronized -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::Synchronized<T, Mutex>: bundles a value with its mutex so
// the value is inaccessible without holding the lock.
//
// Access patterns:
//   wlock()        — returns a LockedPtr<T> holding an exclusive lock.
//                    Dereferencing gives T&. Lock released on destruction.
//   rlock()        — shared (read) lock; gives const T&.
//   withWLock(f)   — calls f(T&) under exclusive lock; returns f's result.
//   withRLock(f)   — calls f(const T&) under shared lock.
//   ulock()        — upgradeable read lock (can be promoted to write).
//
// Default mutex: std::mutex. Pass folly::SharedMutex as second template
// arg to enable rlock() for concurrent reads.
//
// HPC relevance: eliminates the entire class of lock/data mismatch bugs
// at compile time. The locked pointer pattern also makes critical
// sections visually obvious.
// ─────────────────────────────────────────────────────────────────────

using SyncVec = folly::Synchronized<std::vector<int>, folly::SharedMutex>;

int main()
{
    // ── wlock / rlock ─────────────────────────────────────────────────
    SyncVec sv;

    // Exclusive write — lock held for the duration of the scope
    {
        auto locked = sv.wlock();
        locked->push_back(1);
        locked->push_back(2);
        locked->push_back(3);
    } // lock released here

    // Shared read — multiple threads can hold rlock() simultaneously
    {
        auto locked = sv.rlock();
        std::cout << "size: " << locked->size() << "\n"; // 3
        for (int x : *locked) std::cout << x << " ";
        std::cout << "\n";
    }

    // ── withWLock / withRLock — lambda form ───────────────────────────
    sv.withWLock([](auto& vec) { vec.push_back(4); });

    int sum = sv.withRLock([](const auto& vec) {
        int s = 0;
        for (int x : vec) s += x;
        return s;
    });
    std::cout << "sum: " << sum << "\n"; // 10

    // ── Multi-threaded write race prevented ───────────────────────────
    SyncVec shared;
    {
        auto lk = shared.wlock();
        lk->reserve(200);
    }

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back([&shared, t] {
            for (int i = 0; i < 50; ++i)
                shared.withWLock([&](auto& v) { v.push_back(t * 50 + i); });
        });
    for (auto& w : workers) w.join();

    std::cout << "concurrent writes: " << shared.rlock()->size() << " items\n"; // 200

    // ── ulock: upgradeable read lock ──────────────────────────────────
    // Avoids the read → release → re-acquire-write pattern that can
    // cause a TOCTOU race.
    sv.withULockPtr([](auto ulock) {
        if (ulock->back() == 4) {
            auto wlock = ulock.moveFromUpgradeToWrite(); // atomic upgrade
            wlock->push_back(5);
        }
    });
    std::cout << "after upgrade: " << sv.rlock()->back() << "\n"; // 5
}
