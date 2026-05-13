# Folly

Folly (Facebook Open-source Library) is Meta's production C++ library.
It fills gaps left by the stdlib and Boost, with a strong focus on
performance at scale: SIMD hash maps, lock-free queues, composable futures,
zero-copy I/O buffers, and work-stealing thread pools.

---

## Install

```
macOS:  brew install folly
Linux:  apt install libfolly-dev   # Ubuntu 22.04+
        # or build from source: https://github.com/facebook/folly
```

Folly depends on: glog, gflags, fmt, double-conversion, boost, libevent, OpenSSL.
On macOS all deps arrive transitively via Homebrew.

---

## Common compile flags

Most folly headers need at least:

```
-std=c++17 -lfolly -lglog -lgflags -lfmt -ldouble-conversion \
-lboost_system -pthread
```

The per-file comments include the minimal set for that translation unit.

---

## Components covered

| File | Component | Header-only |
|------|-----------|-------------|
| 01 | `folly::fbvector<T>` | yes |
| 02 | `F14FastMap` / `F14VectorMap` | yes |
| 03 | `ProducerConsumerQueue` / `MPMCQueue` | yes |
| 04 | `folly::Synchronized<T>` | yes |
| 05 | `folly::IOBuf` | no (`-lfolly`) |
| 06 | `folly::Future` / `Promise` | no (`-lfolly`) |
| 07 | `CPUThreadPoolExecutor` / `IOThreadPoolExecutor` | no (`-lfolly`) |
| 08 | `folly::small_vector<T, N>` | yes |
| 09 | `folly::Arena` / `SysArena` | yes |
| 10 | `folly::SharedMutex` | yes |

---

## `folly::fbvector<T>`

A drop-in for `std::vector` that cooperates with jemalloc's
`xallocx` to resize in-place when possible, eliminating the
copy-on-grow cost. Also uses `FOLLY_LIKELY` branch hints and avoids
the 2× growth overshoot of most stdlib implementations.

---

## `F14FastMap` / `F14VectorMap`

Folly's family of flat hash maps use SIMD (SSE2 / NEON) to probe 16
slots in a single instruction. `F14FastMap` stores key-value pairs
inline in the table. `F14VectorMap` stores them in a side vector for
stable pointers and better cache behaviour on large values.
Both are significantly faster than `std::unordered_map` for integer
and short-string keys.

---

## `ProducerConsumerQueue` / `MPMCQueue`

`ProducerConsumerQueue<T>` is a wait-free SPSC ring buffer (same
pattern as `boost::lockfree::spsc_queue`). `MPMCQueue<T>` is a
blocking MPMC queue with `blockingWrite` / `blockingRead` and
`tryWrite` / `tryRead` variants. The MPMC variant uses a ticket-
based algorithm with exponential backoff.

---

## `folly::Synchronized<T>`

Bundles a value and its mutex together so you cannot access the value
without holding the lock. `wlock()` returns a `LockedPtr` that
releases the lock on destruction. `rlock()` acquires a shared lock.
`withWLock(f)` / `withRLock(f)` accept a callable for scoped access.
Eliminates the class of bug where a mutex and its guarded data are
accidentally accessed without the lock.

---

## `folly::IOBuf`

A reference-counted, chainable byte buffer designed for zero-copy I/O.
Each `IOBuf` has `headroom` (prepend space) and `tailroom` (append space)
inside a single allocation. Buffers can be chained into a linked list
(`appendChain`) and the whole chain iterated with `IOBuf::Iterator`.
Used internally by Proxygen (Meta's HTTP server) and Thrift.

---

## `folly::Future` / `Promise`

Composable async values. `Promise<T>` produces a `Future<T>`.
`.thenValue(f)` chains transformations. `.via(executor)` routes
continuation execution to a specific thread pool. `collectAll` /
`collectAny` combine multiple futures. Unlike `std::future`,
folly futures support continuations without blocking.

---

## `CPUThreadPoolExecutor` / `IOThreadPoolExecutor`

Work-stealing thread pools. `CPUThreadPoolExecutor` is sized to
`hardware_concurrency` and used for CPU-bound tasks. `IOThreadPoolExecutor`
uses libevent for async I/O dispatch. Both implement `folly::Executor`
so they compose with `Future::via()`. Tasks are submitted with `add(f)`.

---

## `folly::small_vector<T, N>`

Like `boost::container::small_vector`: stores up to N elements inline
(on the stack), heap-allocates only when the count exceeds N. Used
heavily in compiler IR and graph node adjacency lists where the common
case is ≤ 4 elements.

---

## `folly::Arena` / `SysArena`

Bump allocators for short-lived objects. `Arena` allocates from a
provided block; `SysArena` manages its own `mmap`-ed blocks.
`allocate(size)` is O(1) with no per-object free — the whole arena
is released in one shot. Ideal for request-scoped allocations in servers.

---

## `folly::SharedMutex`

A reader-writer lock optimised for read-heavy workloads. Uses a
token-passing protocol to prevent writer starvation while keeping
reader acquisition to a single atomic operation in the uncontended
case. `lock_shared` / `unlock_shared` for readers;
`lock` / `unlock` for writers. Compatible with `std::shared_lock`.
