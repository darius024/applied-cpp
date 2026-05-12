#include <iostream>
#include <thread>
#include <mutex>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Mutex and lock wrappers.
//
// lock_guard:  simple RAII scope lock — locks on construction,
//              unlocks on destruction. Cannot unlock early.
//
// unique_lock: flexible RAII lock — can unlock/relock manually.
//              Required by condition_variable.
//
// scoped_lock: locks multiple mutexes in one statement using internal
//              deadlock-avoidance ordering. Prefer over nested locks.
// ─────────────────────────────────────────────────────────────────────

int counter = 0;
std::mutex mtx;

void increment_guard()
{
    std::lock_guard<std::mutex> lk(mtx); // locked
    ++counter;
} // unlocked here

void increment_unique()
{
    std::unique_lock<std::mutex> lk(mtx);
    ++counter;
    lk.unlock(); // release early — fine for work that doesn't need the lock
    // ... non-shared work here ...
}

// ── Locking two mutexes safely ────────────────────────────────────────
// If two threads each lock m1 then m2 in opposite order, they deadlock.
// scoped_lock avoids this: it always acquires both in a consistent order.
std::mutex m1, m2;

void transfer_a_to_b()
{
    std::scoped_lock lk(m1, m2); // both locked, deadlock-free
    // modify shared state protected by m1 and m2
}

// ── Deadlock (commented out — for illustration only) ──────────────────
// Thread A: lock(m1) → lock(m2)
// Thread B: lock(m2) → lock(m1)
// → circular wait → neither thread can proceed.
// Fix: scoped_lock(m1, m2) on both threads, or always lock in the same order.

int main()
{
    // 10 threads incrementing the same counter
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i)
        threads.emplace_back(increment_guard);
    for (auto& t : threads) t.join();

    std::cout << "counter=" << counter << "\n"; // always 10
}
