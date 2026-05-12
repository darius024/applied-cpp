# Concurrency in C++

C++ concurrency is built on threads, mutual exclusion, condition variables,
atomic operations, and futures — all in the standard library headers
`<thread>`, `<mutex>`, `<condition_variable>`, `<atomic>`, and `<future>`.

---

## Threads

`std::thread` represents one OS thread. You **must** either `join()` it (wait
for it to finish) or `detach()` it before the thread object is destroyed,
otherwise `std::terminate` is called.

`std::jthread` (C++20) joins automatically in its destructor and supports
**cooperative cancellation** via `std::stop_token`.

---

## Mutual exclusion

Shared mutable state requires a mutex. Access without one is a data race.

| Lock wrapper   | Multi-mutex | Unlock early | Use when                           |
|----------------|-------------|--------------|-------------------------------------|
| `lock_guard`   | No          | No           | Simple scope lock                   |
| `unique_lock`  | No          | Yes          | Condition variables, deferred lock  |
| `scoped_lock`  | Yes         | No           | Lock several mutexes safely at once |

`scoped_lock(m1, m2)` uses deadlock-avoidance ordering internally, so you
never need to agree on a global lock order manually.

---

## Condition variables

Block a thread until a condition becomes true. Always pair with `unique_lock`
and the **predicate overload** of `wait()`:

```cpp
cv.wait(lock, []{ return ready; });
```

Without the predicate, **spurious wakeups** (the OS waking the thread for no
reason) cause the code to proceed before the condition is actually true.
The predicate overload loops internally, re-checking after every wakeup.

---

## Atomics

`std::atomic<T>` provides lock-free read/modify/write on scalar types.
No mutex needed; the CPU guarantees each operation is indivisible.

`compare_exchange_weak(expected, desired)`: atomically sets the value to
`desired` only if it currently equals `expected`. Updates `expected` on
failure. The "weak" variant can spuriously fail on some architectures — use
in a loop.

Atomics do **not** make complex objects thread-safe. Only the single atomic
operation is safe. For structs where multiple fields must change together,
use a mutex.

---

## Memory model

Every atomic access has a memory order controlling compiler and CPU reordering:

| Order           | Guarantee                                                      |
|-----------------|----------------------------------------------------------------|
| `relaxed`       | Only atomicity — no ordering between other operations          |
| `acquire`       | Reads after this see all writes before the matching `release`  |
| `release`       | All writes before this are visible to the matching `acquire`   |
| `seq_cst`       | Global sequential consistency — the safe default              |

Use `seq_cst` by default. Weaker orders are micro-optimisations that require
careful reasoning — only reach for them with benchmarks and proofs.

A **data race** — concurrent unsynchronised access where at least one is a
write — is **undefined behaviour** in C++.

---

## `std::async` / `std::future` / `std::promise`

| Type                  | Role                                                        |
|-----------------------|-------------------------------------------------------------|
| `std::async`          | Runs a callable, returns a `future`                         |
| `std::future<T>`      | One-time read of a result; `.get()` blocks until ready      |
| `std::promise<T>`     | Write end of a future — set a value or exception manually   |
| `std::packaged_task`  | Wraps a callable; its future captures the return value      |

`launch::async` guarantees a new thread. `launch::deferred` runs lazily on
the calling thread at `.get()`. Exceptions thrown in the async task are
re-thrown at `.get()`.

---

## Task queues and async handlers

A background thread draining a `std::queue<std::function<void()>>` is the
simplest async handler pattern. Tasks are posted under a mutex and the worker
blocks on a condition variable when idle. Tasks execute outside the lock so
the producer is never blocked by the work.

---

## Thread pool

A fixed set of worker threads sharing one task queue:

1. Workers block on `condition_variable` when the queue is empty.
2. `submit(f)` wraps `f` in a `packaged_task`, pushes it, and returns a future.
3. Shutdown: set a `done` flag, `notify_all`, then `join` all workers.

The pool size is typically `hardware_concurrency()`.

---

## Timers

There is no standard timer in C++17/20. Common patterns:

- **One-shot**: sleep a background thread until a time point, then fire the callback.
- **Repeating**: loop with `sleep_until(next)`, advance `next` by the interval each tick.
- **Cancellation**: signal an `atomic<bool>` flag. The sleeping thread checks it
  after waking. For early cancellation before the sleep expires, use a condition
  variable with a timed wait instead.
