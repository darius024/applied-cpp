# Abseil

Abseil is Google's open-source C++ library — the foundation of
Chrome, TensorFlow, gRPC, and most Google production services.
It prioritises performance, correctness, and tooling integration
(thread-safety annotations, sanitizer support, Bazel/CMake).

---

## Install

```
macOS:  brew install abseil
Linux:  apt install libabsl-dev   # Ubuntu 22.04+
        # or build from source: https://github.com/abseil/abseil-cpp
```

---

## Common compile flags

```
-std=c++17 -labsl_base -labsl_hash -labsl_strings -labsl_time \
-labsl_synchronization -labsl_status -labsl_log_internal_message \
-labsl_log_internal_check_op
```

Per-file comments list the minimal set for that translation unit.

---

## Components covered

| File | Component | Header-only |
|------|-----------|-------------|
| 01 | `absl::flat_hash_map` / `flat_hash_set` | no |
| 02 | `absl::btree_map` / `btree_set` | no |
| 03 | `absl::InlinedVector<T, N>` | no |
| 04 | `absl::Span<T>` | yes |
| 05 | `absl::Status` / `StatusOr<T>` | no |
| 06 | `StrCat`, `StrJoin`, `StrSplit`, `StrFormat`, `StrAppend` | no |
| 07 | `absl::Time` / `absl::Duration` | no |
| 08 | `absl::Mutex` + `GUARDED_BY` / `CondVar` | no |
| 09 | `absl::Notification` / `Barrier` / `BlockingCounter` | no |
| 10 | `absl::FunctionRef` / `AnyInvocable` | no |

---

## `absl::flat_hash_map` / `flat_hash_set`

Open-addressing hash table using SSE2/NEON to probe 16 metadata
bytes (the "control bytes") per cacheline. Each slot stores a
7-bit hash fingerprint; a mismatch in the fingerprint eliminates
the full key comparison. In benchmarks this beats `std::unordered_map`
by 2–5× and is competitive with `folly::F14FastMap`.

Heterogeneous lookup (`find(string_view)` on a `map<string, …>`)
avoids constructing a `std::string` key. Enabled by providing
`absl::Hash` and `std::equal_to<>` or a custom `Eq`.

---

## `absl::btree_map` / `btree_set`

Cache-friendly ordered containers backed by a B-tree. Nodes hold
multiple keys, reducing pointer chasing and memory overhead vs
`std::map` (which allocates one node per element). Suitable for
large sorted maps where iteration and range queries are frequent.
API matches `std::map` / `std::set`.

---

## `absl::InlinedVector<T, N>`

Stores up to N elements inline in the object; heap-allocates beyond
N. Functionally identical to `folly::small_vector`. Used pervasively
in Abseil and TensorFlow for small-size containers. API matches
`std::vector`.

---

## `absl::Span<T>`

A non-owning, bounds-checked view over a contiguous sequence. Used
to write functions that accept arrays, vectors, or raw pointers
without overloading. `Span<const T>` is implicitly constructible
from `vector<T>`, `array<T, N>`, and `T[]`. Precursor to `std::span`
(C++20).

---

## `absl::Status` / `absl::StatusOr<T>`

`Status` carries an error code (`absl::StatusCode`) and a message
string. `StatusOr<T>` combines a `Status` with an optional value —
the canonical error-propagation type in all Google C++ code.

`ABSL_RETURN_IF_ERROR(expr)` macro: if `expr` returns a non-OK
status, propagates it from the current function.
`ABSL_ASSIGN_OR_RETURN(var, expr)` assigns the value or returns
the error.

---

## String utilities

| Function | Purpose |
|----------|---------|
| `absl::StrCat(…)` | Concatenate strings/numbers in one allocation |
| `absl::StrAppend(&s, …)` | Append in-place; avoids extra allocation |
| `absl::StrJoin(range, sep)` | Join a container with separator |
| `absl::StrSplit(s, delim)` | Split into vector of `string_view` |
| `absl::StrFormat(fmt, …)` | Type-safe `printf`; format string checked at compile time |
| `absl::SimpleAtoi(s, &v)` | Fast string → integer |

---

## `absl::Time` / `absl::Duration`

`Duration` is a signed nanosecond-precision time interval.
`Time` is an absolute time point. Both use `int128` internally
to avoid floating-point precision issues. `absl::Now()` returns
the current wall clock. Civil time (`absl::CivilSecond` etc.)
handles calendar arithmetic, DST, leap seconds.

---

## `absl::Mutex` + `GUARDED_BY`

A deadlock-detecting mutex. The clang thread-safety analysis
plugin reads `GUARDED_BY(mu)` annotations at compile time and
warns if a guarded variable is accessed without the lock.
`ReaderMutexLock` acquires a shared lock. `CondVar` integrates
with `Mutex` for wait/notify.

---

## Synchronisation primitives

| Type | Use |
|------|-----|
| `absl::Notification` | One-shot: `Notify()` / `WaitForNotification()` |
| `absl::Barrier` | Rendezvous: all N threads call `Block()` before any continues |
| `absl::BlockingCounter` | Countdown latch: `DecrementCount()` / `Wait()` |

---

## `absl::FunctionRef<Sig>` / `absl::AnyInvocable<Sig>`

`FunctionRef<R(Args…)>`: non-owning reference to any callable.
Zero heap allocation; stores only a pointer and a thunk. Used for
function parameters that must not outlive the call site.

`AnyInvocable<R(Args…)>`: owning, move-only alternative to
`std::function`. Avoids the double-indirection of `std::function`
and supports move-only lambdas (captureing `unique_ptr` etc.).
