#include <iostream>
#include <mutex>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// Problem: if an exception fires between lock() and unlock(), the mutex
// stays locked forever — every thread waiting on it deadlocks.
//
// RAII solves this the same way it solves memory: tie unlock() to the
// destructor so it runs unconditionally when the scope exits.
// ─────────────────────────────────────────────────────────────────────

std::mutex g_mutex;
int        g_counter = 0;

// BAD — manual lock/unlock leaks the lock on exception.
void unsafe_increment()
{
    g_mutex.lock();
    ++g_counter;
    throw std::runtime_error("oops"); // unlock() never called → deadlock
    g_mutex.unlock();
}

// ─────────────────────────────────────────────────────────────────────
// std::lock_guard — the simplest RAII mutex wrapper.
// Locks on construction, unlocks on destruction. Non-copyable, non-movable.
// Use this when you lock for the entire function scope.
// ─────────────────────────────────────────────────────────────────────

void safe_increment()
{
    std::lock_guard<std::mutex> lock(g_mutex); // locked here
    ++g_counter;
    // exception or return → destructor unlocks automatically
}

// ─────────────────────────────────────────────────────────────────────
// std::unique_lock — more flexible: supports deferred locking,
// try_lock, timed_lock, and manual unlock/re-lock mid-scope.
// Slightly heavier than lock_guard; use when you need the flexibility.
// ─────────────────────────────────────────────────────────────────────

void work_with_unlock()
{
    std::unique_lock<std::mutex> lock(g_mutex);

    ++g_counter;

    lock.unlock();       // release early to do expensive work outside the lock
    // ... expensive work, no mutex held ...
    lock.lock();         // re-acquire for final update

    ++g_counter;
}  // destructor unlocks if still held

int main()
{
    safe_increment();
    work_with_unlock();
    std::cout << "counter = " << g_counter << " (expected 3)\n";

    // Demonstrate that unsafe_increment would deadlock:
    // safe_increment() would hang after unsafe_increment() throws.
    // That call is intentionally omitted.
}
