#include <folly/ProducerConsumerQueue.h>
#include <folly/MPMCQueue.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

// compile: g++ -std=c++17 03_queues.cpp -o 03_queues -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::ProducerConsumerQueue<T>  — wait-free SPSC ring buffer.
//   write(v)   / read(v)    — non-blocking; return bool.
//   writeIfNotFull / readIfNotEmpty — aliases with clearer names.
//   Capacity is set at construction; T must be move-constructible.
//
// folly::MPMCQueue<T>  — blocking MPMC queue.
//   blockingWrite(v)   — waits until space is available.
//   blockingRead(v)    — waits until an item is available.
//   tryWrite(v)        — non-blocking; returns bool.
//   tryRead(v)         — non-blocking; returns bool.
//   Uses a ticket-based algorithm: each slot has a sequence number;
//   producers CAS the write ticket, consumers CAS the read ticket.
//   Exponential backoff prevents thundering-herd on contention.
//
// HPC relevance: SPSC is the canonical hand-off between a network
// receive thread and a processing thread. MPMC suits worker pools
// where many threads both enqueue and dequeue tasks.
// ─────────────────────────────────────────────────────────────────────

// ── 1. ProducerConsumerQueue (SPSC) ───────────────────────────────────
void demo_spsc()
{
    folly::ProducerConsumerQueue<int> q(1024); // capacity must be a power of two
    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (int i = 0; i < 20; ++i) {
            while (!q.write(i)) {} // spin if full (won't happen at capacity 1024)
        }
        done = true;
    });

    int sum = 0;
    while (!done.load(std::memory_order_relaxed) || !q.isEmpty()) {
        int v;
        if (q.read(v)) sum += v;
    }

    producer.join();
    std::cout << "SPSC sum: " << sum << "\n"; // 0+…+19 = 190
}

// ── 2. MPMCQueue (multi-producer, multi-consumer) ─────────────────────
void demo_mpmc()
{
    folly::MPMCQueue<int> q(128);
    const int N = 4, items_each = 25; // 4 producers × 25 = 100 items

    std::vector<std::thread> producers;
    for (int t = 0; t < N; ++t)
        producers.emplace_back([&, t] {
            for (int i = 0; i < items_each; ++i)
                q.blockingWrite(t * 100 + i);
        });

    // 2 consumer threads drain everything
    std::atomic<int> total{0};
    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c)
        consumers.emplace_back([&] {
            int v;
            while (total.load() < N * items_each) {
                if (q.readIfEmpty(v)) {
                    // readIfEmpty returns true when the queue WAS empty — skip
                } else if (q.tryRead(v)) {
                    total.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });

    for (auto& p : producers) p.join();
    for (auto& c : consumers) c.join();

    std::cout << "MPMC total consumed: " << total.load() << "\n"; // 100

    // ── tryWrite / tryRead for non-blocking use ───────────────────────
    folly::MPMCQueue<std::string> nb(4);
    nb.tryWrite("a");
    nb.tryWrite("b");
    std::string s;
    while (nb.tryRead(s)) std::cout << "nb: " << s << "\n";
}

int main()
{
    demo_spsc();
    demo_mpmc();
}
