#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────
// Data races and memory ordering.
//
// A DATA RACE is concurrent access to the same memory where:
//   - at least one access is a write, AND
//   - the accesses are not synchronised.
// Data races are UNDEFINED BEHAVIOUR in C++ — the program may produce
// wrong results, crash, or be silently miscompiled.
//
// Two fixes: mutex (coarse, safe) or atomic (fine-grained).
//
// MEMORY ORDERS control how the CPU and compiler may reorder instructions
// around an atomic operation:
//
//   relaxed    — only atomicity; no ordering relative to other reads/writes.
//                Use for statistics counters where stale reads are fine.
//
//   release    — all writes BEFORE this store are visible to any thread
//                that acquires the same variable.
//
//   acquire    — all reads AFTER this load see the writes that preceded
//                the matching release.
//
//   seq_cst    — globally consistent order across all threads. The default.
//                Safe to use everywhere; small cost on some architectures.
//
// Rule of thumb: use seq_cst unless you have a measured performance problem
// and a proof that a weaker order is correct.
// ─────────────────────────────────────────────────────────────────────

// ── DATA RACE example (do NOT uncomment — UB) ─────────────────────────
// int shared = 0;
// void bad_write() { shared = 1; }
// void bad_read()  { std::cout << shared; }
// std::thread(bad_write), std::thread(bad_read)  ← DATA RACE

// ── Fix 1: mutex ──────────────────────────────────────────────────────
int shared_val = 0;
std::mutex smtx;

void safe_write(int v) { std::lock_guard lk(smtx); shared_val = v; }
int  safe_read()       { std::lock_guard lk(smtx); return shared_val; }

// ── Fix 2: acquire/release pairing ───────────────────────────────────
// publisher stores data, then flags ready with release.
// subscriber acquires the flag — this guarantees it sees data=42.
// Without acquire/release the CPU could reorder the reads/writes and
// subscriber might see flag=true but data=0.

std::atomic<int>  data{0};
std::atomic<bool> flag{false};

void publisher()
{
    data.store(42, std::memory_order_relaxed);    // 1. write data
    flag.store(true, std::memory_order_release);  // 2. signal (release)
}

void subscriber()
{
    while (!flag.load(std::memory_order_acquire)) // 3. wait for signal (acquire)
        std::this_thread::yield();
    // acquire guarantees we now see the data written before the release
    std::cout << "data=" << data.load(std::memory_order_relaxed) << "\n"; // 42
}

int main()
{
    std::thread w(publisher);
    std::thread r(subscriber);
    w.join(); r.join();

    // ── mutex fix demo ────────────────────────────────────────────────
    safe_write(99);
    std::cout << "safe_read=" << safe_read() << "\n"; // 99
}
