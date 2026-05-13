#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

// compile: g++ -std=c++17 07_executors.cpp -o 07_executors \
//          -lfolly -lglog -lgflags -lfmt -ldouble-conversion \
//          -lboost_system -pthread

// ─────────────────────────────────────────────────────────────────────
// folly executors: work-stealing thread pools.
//
// CPUThreadPoolExecutor
//   — For CPU-bound tasks (compute, serialisation, compression).
//   — Typically sized to hardware_concurrency().
//   — Uses a lock-free work-stealing deque per thread; idle threads
//     steal tasks from busy threads' queues.
//   — add(f) submits a Func; returns void (fire-and-forget).
//
// IOThreadPoolExecutor
//   — For I/O-bound tasks (sockets, timers, file I/O).
//   — Each thread owns a libevent EventBase; tasks are dispatched as
//     EventBase callbacks.
//   — Use getEventBase() to get the EventBase for timer/socket work.
//
// Both implement folly::Executor so they compose with Future::via().
//
// HPC relevance: separating CPU and I/O work to dedicated pools
// prevents one slow I/O call from blocking a CPU worker, and vice
// versa. Work-stealing maximises CPU utilisation without a global lock.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    const int n_cpu = static_cast<int>(std::thread::hardware_concurrency());

    // ── CPUThreadPoolExecutor ─────────────────────────────────────────
    folly::CPUThreadPoolExecutor cpu_pool(n_cpu);

    std::atomic<int> sum{0};
    const int tasks = 100;

    for (int i = 0; i < tasks; ++i)
        cpu_pool.add([&sum, i] {
            sum.fetch_add(i, std::memory_order_relaxed);
        });

    // Wait for all tasks via a sentinel future
    folly::via(&cpu_pool, [] {}).wait();
    // All prior tasks are guaranteed complete now
    std::cout << "CPUPool sum: " << sum.load() << "\n"; // 4950

    // ── via() integration with Future ────────────────────────────────
    std::vector<folly::Future<int>> futs;
    futs.reserve(8);
    for (int i = 0; i < 8; ++i)
        futs.push_back(folly::via(&cpu_pool, [i] { return i * i; }));

    auto all = folly::collectAll(std::move(futs)).get();
    std::cout << "squares: ";
    for (auto& t : all) std::cout << t.value() << " ";
    std::cout << "\n";

    // ── Thread pool stats ─────────────────────────────────────────────
    auto stats = cpu_pool.getPoolStats();
    std::cout << "pool threads:  " << stats.threadCount  << "\n";
    std::cout << "pending tasks: " << stats.pendingTaskCount << "\n";

    // ── IOThreadPoolExecutor ──────────────────────────────────────────
    // Each thread owns an EventBase for async I/O / timer dispatch.
    folly::IOThreadPoolExecutor io_pool(2);

    std::atomic<int> io_count{0};
    for (int i = 0; i < 10; ++i)
        io_pool.add([&io_count] {
            // In real code: schedule async I/O on getEventBase()
            io_count.fetch_add(1, std::memory_order_relaxed);
        });

    folly::via(&io_pool, [] {}).wait();
    std::cout << "IOPool tasks ran: " << io_count.load() << "\n"; // 10

    // ── join (drain all pending work and stop threads) ────────────────
    cpu_pool.join();
    io_pool.join();
}
