#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Lock-free programming: CAS, memory ordering, SPSC queue.
//
// std::atomic<T> — the fundamental primitive.
//   Any read or write is indivisible (atomic).
//   Without explicit memory_order, every operation defaults to
//   memory_order_seq_cst — the strongest and most expensive order.
//
// Memory orders (weakest → strongest):
//   relaxed    — no reordering guarantees; only atomicity.
//                Use for counters/flags where ordering doesn't matter.
//   acquire    — no loads/stores in the current thread may be
//                reordered to appear BEFORE this load.
//   release    — no loads/stores in the current thread may be
//                reordered to appear AFTER this store.
//   acq_rel    — both acquire and release; for RMW ops (fetch_add, CAS).
//   seq_cst    — total order across all threads; full fence.
//
//   Canonical producer/consumer pair:
//     producer: store(data, release)   → consumer: load(acquire)
//   This ensures the consumer sees all writes done before the release.
//
// Compare-and-swap (CAS):
//   bool compare_exchange_weak(expected, desired, success_order, fail_order)
//     If *this == expected: set *this = desired, return true.
//     Else: set expected = *this, return false.
//   _weak may spuriously fail; use in a loop.
//   _strong never spuriously fails; use for single-try semantics.
//
// SPSC queue (Single-Producer Single-Consumer):
//   The canonical HFT lock-free pattern.
//   - One writer thread, one reader thread.
//   - No CAS needed: each index is written by exactly one thread.
//   - Only two atomic loads/stores per enqueue/dequeue.
//   - Padding prevents false sharing between head and tail.
//
// ABA problem:
//   CAS on a pointer: A → B → A between threads looks like no change.
//   Mitigations: tagged pointer (embed a counter), epoch reclamation.
//
// Compile:  g++ -std=c++20 -O2 08_lockfree.cpp -o lockfree && ./lockfree
// ─────────────────────────────────────────────────────────────────────

// ── PART 1: memory ordering — acquire/release pair ────────────────────
static void demo_acquire_release()
{
    std::puts("── acquire/release ordering ─────────────────────");

    struct SharedData {
        alignas(64) std::atomic<int>  ready{0};
        alignas(64) double            price{0.0};
        alignas(64) int               qty{0};
    };

    SharedData sd;

    // Producer thread: write data then release-store the flag.
    std::thread producer([&] {
        sd.price = 101.5;       // plain write — safe because the
        sd.qty   = 200;         // release below creates the barrier.
        sd.ready.store(1, std::memory_order_release);
        // release: nothing above this line moves below it.
    });

    // Consumer thread: acquire-load the flag, then read data.
    std::thread consumer([&] {
        while (sd.ready.load(std::memory_order_acquire) == 0)
            ; // spin — acquire: nothing below this line moves above it.
        // Guaranteed to see price=101.5 and qty=200.
        std::printf("  consumer: price=%.1f  qty=%d\n", sd.price, sd.qty);
    });

    producer.join();
    consumer.join();
}

// ── PART 2: CAS — lock-free counter increment ─────────────────────────
static void demo_cas()
{
    std::puts("\n── CAS loop (lock-free counter) ─────────────────");

    std::atomic<int> counter{0};
    const int N_THREADS = 4;
    const int OPS_EACH  = 100'000;

    std::vector<std::thread> threads;
    threads.reserve(N_THREADS);

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < OPS_EACH; ++i) {
                int expected = counter.load(std::memory_order_relaxed);
                // CAS loop: retry until we successfully increment.
                while (!counter.compare_exchange_weak(
                            expected,
                            expected + 1,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed))
                    ; // expected is updated to the current value on failure
            }
        });
    }

    for (auto& th : threads) th.join();
    std::printf("  counter = %d  (expected %d)\n",
                counter.load(), N_THREADS * OPS_EACH);
    std::puts("  (fetch_add is simpler; CAS loop shown for illustration)");

    // fetch_add version — preferred for simple increment:
    std::atomic<int> c2{0};
    for (int t = 0; t < N_THREADS; ++t) {
        threads[static_cast<std::size_t>(t)] = std::thread([&] {
            for (int i = 0; i < OPS_EACH; ++i)
                c2.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& th : threads) th.join();
    std::printf("  fetch_add counter = %d\n", c2.load());
}

// ── PART 3: SPSC ring queue ───────────────────────────────────────────
// The gold-standard for latency-critical thread hand-off in HFT.
// One producer, one consumer.  No CAS; only atomic loads and stores.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
public:
    // Returns false if the queue is full.
    bool try_push(const T& value) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        buf_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Returns nullopt if the queue is empty.
    std::optional<T> try_pop() noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T value = buf_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return value;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire)
            == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;
    T buf_[Capacity]{};

    // Separate cache lines to avoid false sharing between producer
    // (owns head_) and consumer (owns tail_).
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

static void demo_spsc()
{
    std::puts("\n── SPSC queue ───────────────────────────────────");

    SpscQueue<std::int64_t, 1024> q;
    const int N = 100'000;

    auto t0 = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.try_push(static_cast<std::int64_t>(i)))
                ; // spin-wait if full
        }
    });

    long long sum = 0;
    std::thread consumer([&] {
        int received = 0;
        while (received < N) {
            if (auto v = q.try_pop()) {
                sum += *v;
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    auto t1 = std::chrono::steady_clock::now();
    double ns_per = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / N;

    // sum should equal N*(N-1)/2
    long long expected = static_cast<long long>(N) * (N - 1) / 2;
    std::printf("  %d messages  sum=%lld (expected %lld)  %.1f ns/msg\n",
                N, sum, expected, ns_per);
}

// ── PART 4: relaxed order for statistics counters ─────────────────────
// When only final totals matter (no happens-before relationship needed),
// relaxed is sufficient and cheaper than acquire/release.
static void demo_relaxed_counter()
{
    std::puts("\n── relaxed order for stats counters ─────────────");

    std::atomic<long> orders_sent{0};
    std::atomic<long> fills_received{0};

    const int N = 50'000;
    std::thread t1([&] {
        for (int i = 0; i < N; ++i)
            orders_sent.fetch_add(1, std::memory_order_relaxed);
    });
    std::thread t2([&] {
        for (int i = 0; i < N / 2; ++i)
            fills_received.fetch_add(1, std::memory_order_relaxed);
    });
    t1.join(); t2.join();

    // Synchronize at join() — all relaxed stores are visible here.
    std::printf("  orders=%ld  fills=%ld  fill_rate=%.1f%%\n",
                orders_sent.load(), fills_received.load(),
                100.0 * fills_received.load()
                        / static_cast<double>(orders_sent.load()));
}

int main()
{
    demo_acquire_release();
    demo_cas();
    demo_spsc();
    demo_relaxed_counter();
}
