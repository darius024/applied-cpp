#include <folly/SharedMutex.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <shared_mutex>  // for std::shared_lock / std::unique_lock

// compile: g++ -std=c++17 10_shared_mutex.cpp -o 10_shared_mutex -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::SharedMutex: reader-writer lock tuned for read-heavy workloads.
//
// Interface is identical to std::shared_mutex, so it works with
// std::shared_lock (readers) and std::unique_lock (writers).
//
// Key improvements over std::shared_mutex:
//   1. Reader acquisition is a single atomic fetch_add in the uncontended
//      case — no cache-line bouncing between readers.
//   2. Token-passing protocol prevents writer starvation while
//      maintaining high reader throughput.
//   3. Reader/writer fairness is tunable via template policy.
//
// SharedMutexWritePriority  — default; pending writers block new readers.
// SharedMutexReadPriority   — readers always make progress; writers
//                             may wait longer (useful if write latency
//                             is not critical).
//
// lock_shared()   / unlock_shared()   — shared (read) lock.
// lock()          / unlock()          — exclusive (write) lock.
// try_lock_shared / try_lock          — non-blocking variants.
//
// HPC relevance: in-memory caches, config objects, routing tables —
// anything read millions of times but written rarely benefits greatly
// from a reader-writer lock.
// ─────────────────────────────────────────────────────────────────────

struct Cache {
    folly::SharedMutex mu;
    int value = 0;
    long reads = 0;  // protected by mu (demo only — normally use atomic)
};

int main()
{
    Cache cache;

    const int N_READERS = 8;
    const int N_WRITERS = 2;
    const int OPS = 500;

    std::vector<std::thread> threads;

    // Reader threads — acquire shared lock
    for (int r = 0; r < N_READERS; ++r)
        threads.emplace_back([&cache] {
            for (int i = 0; i < OPS; ++i) {
                std::shared_lock lock(cache.mu); // shared lock
                // Multiple readers hold this simultaneously
                (void)cache.value;
                cache.reads++;
            }
        });

    // Writer threads — acquire exclusive lock
    for (int w = 0; w < N_WRITERS; ++w)
        threads.emplace_back([&cache] {
            for (int i = 0; i < OPS / 10; ++i) {
                std::unique_lock lock(cache.mu); // exclusive lock
                cache.value++;
            }
        });

    for (auto& t : threads) t.join();

    std::cout << "final value: " << cache.value << "\n";  // 100 (2 × 50)
    std::cout << "total reads: " << cache.reads << "\n";  // 4000 (8 × 500)

    // ── try_lock_shared: non-blocking read attempt ────────────────────
    folly::SharedMutex mu;
    mu.lock(); // exclusive lock held
    std::cout << "try_lock_shared while write-locked: "
              << std::boolalpha << mu.try_lock_shared() << "\n"; // false
    mu.unlock();
    std::cout << "try_lock_shared after unlock: "
              << mu.try_lock_shared() << "\n"; // true
    mu.unlock_shared();

    // ── ReadPriority variant ──────────────────────────────────────────
    // Readers are never blocked by pending writers.
    // Use when write latency is not a concern.
    folly::SharedMutexReadPriority rp_mu;
    {
        std::shared_lock rl(rp_mu);
        std::cout << "ReadPriority shared lock acquired\n";
    }
}
