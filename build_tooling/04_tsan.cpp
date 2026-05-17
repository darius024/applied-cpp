#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// ThreadSanitizer (TSan) — data races, lock-order inversions.
//
// TSan instruments every memory access and records a *vector clock*
// per location.  When a read and a write (or two writes) from different
// threads are observed without a happens-before relationship, it fires.
//
// Overhead: 5–15× memory, 5–15× runtime.
// Cannot be combined with ASan or MSan.
//
// Compile (safe, deterministic output):
//   g++ -std=c++20 -O1 -g -pthread 04_tsan.cpp -o tsan && ./tsan
//
// Compile with TSan (races reported):
//   g++ -std=c++20 -O1 -g -pthread -fsanitize=thread \
//       -fno-omit-frame-pointer \
//       04_tsan.cpp -o tsan && ./tsan
//
// Runtime options:
//   TSAN_OPTIONS=second_deadlock_stack=1:halt_on_error=0:history_size=7 ./tsan
//
// TSan report format:
//   WARNING: ThreadSanitizer: data race (pid=...)
//     Write of size 8 at 0x... by thread T2:
//       #0 increment_price ...
//     Previous write of size 8 at 0x... by thread T1:
//       #0 increment_price ...
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── PART 1: raw data race ─────────────────────────────────────────────
//
// Two threads concurrently write to the same double without any
// synchronisation.  The write is not atomic — on x86 a 64-bit write
// may be torn if not naturally aligned; regardless, the C++ memory model
// makes this UB.
//
// TSan report (with -fsanitize=thread):
//   data race on shared_price
//   Write of size 8 by thread T2 ... Write by thread T1 ...
//
static double g_shared_price = 0.0; // unsynchronised

static void unsafe_write(double price)
{
    g_shared_price = price; // race!
}

static void demo_raw_race()
{
    section("raw data race (unsafe)");
    g_shared_price = 0.0;

    std::thread t1([] { for (int i = 0; i < 10'000; ++i) unsafe_write(100.0 + i); });
    std::thread t2([] { for (int i = 0; i < 10'000; ++i) unsafe_write(200.0 + i); });

    t1.join(); t2.join();
    std::printf("  g_shared_price final = %.1f  (result is non-deterministic)\n",
                g_shared_price);
    std::puts("  TSan would report a data race here");
}

// ── PART 2: race fixed with std::mutex ────────────────────────────────
static double g_mutex_price = 0.0;
static std::mutex g_mutex;

static void demo_mutex_fixed()
{
    section("data race fixed with mutex");
    g_mutex_price = 0.0;

    std::thread t1([&] {
        for (int i = 0; i < 10'000; ++i) {
            std::lock_guard lk{g_mutex};
            g_mutex_price = 100.0 + i;
        }
    });
    std::thread t2([&] {
        for (int i = 0; i < 10'000; ++i) {
            std::lock_guard lk{g_mutex};
            g_mutex_price = 200.0 + i;
        }
    });

    t1.join(); t2.join();
    std::printf("  g_mutex_price final = %.1f  (serialised — no race)\n",
                g_mutex_price);
}

// ── PART 3: race fixed with std::atomic ──────────────────────────────
static std::atomic<double> g_atomic_price{0.0};

static void demo_atomic_fixed()
{
    section("data race fixed with atomic");

    std::thread t1([&] {
        for (int i = 0; i < 10'000; ++i)
            g_atomic_price.store(100.0 + i, std::memory_order_relaxed);
    });
    std::thread t2([&] {
        for (int i = 0; i < 10'000; ++i)
            g_atomic_price.store(200.0 + i, std::memory_order_relaxed);
    });

    t1.join(); t2.join();
    std::printf("  g_atomic_price final = %.1f  (atomic — no race)\n",
                g_atomic_price.load());
}

// ── PART 4: lock-order inversion ──────────────────────────────────────
//
// If thread A acquires lock1 then lock2, and thread B acquires lock2
// then lock1, a deadlock is possible.  TSan detects this even if a
// deadlock does not actually occur in this run.
//
// TSan report:
//   WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)
//
static std::mutex g_lock_orders;
static std::mutex g_lock_fills;

static void thread_send_order()
{
    std::lock_guard lk1{g_lock_orders}; // acquires orders first
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    std::lock_guard lk2{g_lock_fills};  // then fills
    // Do work.
}

static void thread_process_fill()
{
    std::lock_guard lk1{g_lock_fills};  // acquires fills first — inverted!
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    std::lock_guard lk2{g_lock_orders}; // then orders — deadlock risk
    // Do work.
}

static void demo_lock_order_inversion()
{
    section("lock-order inversion (TSan potential deadlock)");

    std::thread t1(thread_send_order);
    std::thread t2(thread_process_fill);
    t1.join(); t2.join();

    std::puts("  two threads acquire two mutexes in opposite order");
    std::puts("  TSan reports a potential deadlock even without one occurring");
    std::puts("  Fix: always acquire locks in the same total order");
    std::puts("       or use std::scoped_lock{m1, m2} (deadlock-free)");
}

// ── PART 5: correct multi-mutex acquisition ───────────────────────────
static std::mutex g_mu_a, g_mu_b;

static void demo_scoped_lock()
{
    section("std::scoped_lock — deadlock-free multi-mutex");

    std::thread t1([&] {
        std::scoped_lock lk{g_mu_a, g_mu_b}; // acquires both atomically
        (void)lk;
    });
    std::thread t2([&] {
        std::scoped_lock lk{g_mu_b, g_mu_a}; // order doesn't matter
        (void)lk;
    });

    t1.join(); t2.join();
    std::puts("  scoped_lock uses std::lock() internally — no deadlock");
}

// ── PART 6: read-write data race ──────────────────────────────────────
// Common pattern: one writer, many readers — needs synchronisation.
static long g_counter = 0;

static void demo_readwrite_race()
{
    section("read/write race");
    g_counter = 0;

    std::thread writer([&] {
        for (int i = 0; i < 100'000; ++i)
            ++g_counter; // write
    });

    long snapshots[3]{};
    std::thread reader([&] {
        for (int i = 0; i < 3; ++i) {
            snapshots[i] = g_counter; // concurrent read — race!
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    writer.join(); reader.join();
    std::printf("  snapshots: %ld %ld %ld  (non-deterministic without sync)\n",
                snapshots[0], snapshots[1], snapshots[2]);
    std::puts("  Fix: use std::atomic<long> g_counter or protect with mutex");
}

int main()
{
    std::puts("=== ThreadSanitizer demo ===");
    demo_raw_race();
    demo_mutex_fixed();
    demo_atomic_fixed();
    demo_lock_order_inversion();
    demo_scoped_lock();
    demo_readwrite_race();
    std::puts("\n=== compile with -fsanitize=thread to see race reports ===");
}
