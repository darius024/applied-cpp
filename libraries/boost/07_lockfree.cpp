#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/stack.hpp>
#include <iostream>
#include <thread>
#include <atomic>

// compile: g++ -std=c++17 07_lockfree.cpp -o 07_lockfree -pthread

// ─────────────────────────────────────────────────────────────────────
// boost::lockfree: wait-free / lock-free concurrent data structures.
// All are header-only. Element type T must be trivially copyable.
//
// spsc_queue  — single-producer / single-consumer ring buffer.
//               Fastest of the three; uses no atomic RMW on the fast path.
//
// queue<T>    — MPMC (multi-producer, multi-consumer) bounded queue.
//               consume_all(f) drains all items in one call.
//
// stack<T>    — LIFO, MPMC. Useful for a free-list / object pool.
//
// HPC relevance: eliminates mutex/condvar overhead on the hot path.
//   spsc_queue is the standard pattern for a producer thread feeding a
//   consumer thread (e.g., network receive → processing pipeline).
// ─────────────────────────────────────────────────────────────────────

// ── 1. spsc_queue — single producer, single consumer ─────────────────
void demo_spsc()
{
    // Capacity must be a power of two and set at compile time.
    boost::lockfree::spsc_queue<int, boost::lockfree::capacity<1024>> q;

    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (int i = 0; i < 10; ++i) {
            while (!q.push(i)) {} // spin if full (won't happen with capacity 1024)
        }
        done = true;
    });

    int sum = 0;
    while (!done || !q.empty()) {
        int v;
        while (q.pop(v)) sum += v; // non-blocking: pop returns false if empty
    }

    producer.join();
    std::cout << "spsc sum: " << sum << "\n"; // 0+1+…+9 = 45
}

// ── 2. lockfree::queue — multi-producer, multi-consumer ──────────────
void demo_mpmc_queue()
{
    boost::lockfree::queue<int, boost::lockfree::capacity<256>> q;

    // Two producers
    auto produce = [&](int start) {
        for (int i = start; i < start + 5; ++i)
            while (!q.push(i)) {}
    };
    std::thread p1(produce, 0), p2(produce, 100);
    p1.join(); p2.join();

    // Drain everything in one call
    int count = 0;
    q.consume_all([&](int) { ++count; });
    std::cout << "mpmc queue consumed: " << count << " items\n"; // 10
}

// ── 3. lockfree::stack — LIFO, MPMC ──────────────────────────────────
void demo_stack()
{
    boost::lockfree::stack<int, boost::lockfree::capacity<64>> s;

    for (int i = 1; i <= 5; ++i) s.push(i);

    // consume_all visits in LIFO order (5, 4, 3, 2, 1).
    std::cout << "stack (LIFO): ";
    s.consume_all([](int v) { std::cout << v << " "; });
    std::cout << "\n";
}

int main()
{
    demo_spsc();
    demo_mpmc_queue();
    demo_stack();
}
