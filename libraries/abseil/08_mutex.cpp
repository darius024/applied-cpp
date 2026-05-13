#include <absl/synchronization/mutex.h>
#include <iostream>
#include <thread>
#include <vector>

// compile: g++ -std=c++17 08_mutex.cpp -o 08_mutex \
//          -labsl_synchronization -labsl_base -labsl_strings \
//          -labsl_time -labsl_stacktrace -labsl_symbolize -pthread

// ─────────────────────────────────────────────────────────────────────
// absl::Mutex
//
// A drop-in for std::mutex with two important additions:
//
// 1. Thread-safety annotations (Clang static analysis):
//    GUARDED_BY(mu)   — member variable only accessible while mu is held.
//    ABSL_LOCKS_EXCLUDED(mu) — function must NOT hold mu when called.
//    These are checked by -Wthread-safety (Clang) at compile time.
//    They are no-ops on other compilers.
//
// 2. Reader/writer locking (like std::shared_mutex):
//    ReaderLock() / ReaderUnlock() — shared read access.
//    ReaderMutexLock(mu)           — RAII shared lock (like shared_lock).
//    MutexLock(mu)                 — RAII exclusive lock (like unique_lock).
//
// absl::CondVar:
//    Works only with absl::Mutex. Signal() / SignalAll() / Wait(mu).
//    WaitWithTimeout(mu, duration) — returns true if timed out.
//
// absl::Mutex also supports Await(mu, condition) — blocks until the
// Condition lambda returns true, re-evaluating on each wake.
// ─────────────────────────────────────────────────────────────────────

// ── Thread-safety annotations ─────────────────────────────────────────
class SafeCounter {
public:
    void increment() ABSL_LOCKS_EXCLUDED(mu_) {
        absl::MutexLock lock(&mu_);
        ++count_;
    }

    int get() const ABSL_LOCKS_EXCLUDED(mu_) {
        absl::ReaderMutexLock lock(&mu_);
        return count_;
    }

private:
    mutable absl::Mutex mu_;
    int count_ ABSL_GUARDED_BY(mu_) = 0;
};

// ── Producer / consumer with CondVar ──────────────────────────────────
struct Queue {
    absl::Mutex mu;
    absl::CondVar cv;
    std::vector<int> items ABSL_GUARDED_BY(mu);
    bool done ABSL_GUARDED_BY(mu) = false;
};

int main()
{
    // ── Concurrent counter ────────────────────────────────────────────
    SafeCounter counter;
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&counter] {
                for (int i = 0; i < 250; ++i) counter.increment();
            });
        for (auto& t : threads) t.join();
    }
    std::cout << "counter: " << counter.get() << "\n"; // 1000

    // ── CondVar: producer / consumer ─────────────────────────────────
    Queue q;

    std::thread producer([&q] {
        for (int i = 0; i < 5; ++i) {
            absl::MutexLock lock(&q.mu);
            q.items.push_back(i);
            q.cv.Signal();
        }
        absl::MutexLock lock(&q.mu);
        q.done = true;
        q.cv.SignalAll();
    });

    std::thread consumer([&q] {
        while (true) {
            absl::MutexLock lock(&q.mu);
            // WaitWithTimeout prevents infinite block if signal is missed
            while (q.items.empty() && !q.done)
                q.cv.WaitWithTimeout(&q.mu, absl::Milliseconds(100));
            while (!q.items.empty()) {
                std::cout << "consumed: " << q.items.back() << "\n";
                q.items.pop_back();
            }
            if (q.done) break;
        }
    });

    producer.join();
    consumer.join();

    // ── Await: block until condition is true ─────────────────────────
    // Await re-evaluates the condition each time the mutex is released.
    // More convenient than a manual while-loop around Wait().
    absl::Mutex mu;
    int value ABSL_GUARDED_BY(mu) = 0;

    std::thread setter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        absl::MutexLock lock(&mu);
        value = 42;
    });

    {
        absl::MutexLock lock(&mu);
        mu.Await(absl::Condition(+[](int* v) { return *v != 0; }, &value));
        std::cout << "await got value: " << value << "\n"; // 42
    }
    setter.join();
}
