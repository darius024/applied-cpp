#include <iostream>
#include <atomic>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// std::atomic<T>: lock-free, indivisible operations on scalar types.
// No mutex needed; the CPU guarantees each operation completes atomically.
//
// Core operations:
//   load()           — read the value
//   store(v)         — write the value
//   exchange(v)      — swap: write v, return old value
//   fetch_add(n)     — add n, return old value  (also fetch_sub, fetch_or…)
//   compare_exchange_weak(expected, desired)
//     — if value == expected: set value = desired, return true
//       else:                 set expected = value, return false
//     Used in lock-free retry loops. "weak" can spuriously fail.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Basic operations ──────────────────────────────────────────────
    std::atomic<int> val{0};

    val.store(10);
    int old = val.exchange(20);          // swap: old=10, val=20
    std::cout << "old=" << old << " val=" << val.load() << "\n";

    // ── compare_exchange in a retry loop ──────────────────────────────
    // Atomically multiply val by 3. There is no fetch_mul, so we build
    // one from compare_exchange: read current → compute next → try to swap.
    int cur = val.load();
    while (!val.compare_exchange_weak(cur, cur * 3))
        ; // cur updated to actual value on failure; retry
    std::cout << "after *3: val=" << val.load() << "\n"; // 60

    // ── Thread-safe counter — no mutex ────────────────────────────────
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&counter]{ counter.fetch_add(1); });
    for (auto& t : threads) t.join();
    std::cout << "counter=" << counter.load() << "\n"; // always 8

    // ── Atomic flag for signalling ────────────────────────────────────
    std::atomic<bool> ready{false};
    std::atomic<int>  result{0};

    std::thread worker([&]{
        while (!ready.load(std::memory_order_acquire))
            std::this_thread::yield();    // spin until signalled
        result.store(42, std::memory_order_relaxed);
    });

    ready.store(true, std::memory_order_release); // release: worker sees all prior writes
    worker.join();
    std::cout << "result=" << result.load() << "\n"; // 42

    // ── Atomics are not a silver bullet ───────────────────────────────
    // std::atomic only guarantees the single operation is indivisible.
    // If two fields must change together (e.g., a balance + transaction log),
    // atomic does not help — use a mutex to protect the invariant.
}
