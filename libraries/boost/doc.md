# Boost Libraries

Boost is a peer-reviewed collection of portable C++ libraries. Many components
were eventually standardised (`filesystem → std::filesystem`, `regex → std::regex`,
`optional`, `variant`, `any`, `thread`). The ones below remain valuable because
they either go beyond the stdlib, have better performance characteristics, or
are widely deployed in production HPC and server systems.

---

## Install

```
macOS:  brew install boost
Linux:  apt install libboost-all-dev
```

---

## Header-only vs compiled

| Library          | Link flags needed                                              |
|------------------|----------------------------------------------------------------|
| string_algo      | none (header-only)                                            |
| filesystem       | `-lboost_filesystem -lboost_system`                           |
| program_options  | `-lboost_program_options`                                     |
| log              | `-lboost_log -lboost_log_setup -lboost_thread -lboost_system -pthread -DBOOST_LOG_DYN_LINK` |
| asio             | `-lboost_system -pthread`                                     |
| lockfree         | none (header-only)                                            |
| pool             | none (header-only)                                            |
| circular_buffer  | none (header-only)                                            |
| signals2         | none (header-only)                                            |

---

## `boost/algorithm/string`

String utilities missing from the stdlib: `trim`, `split`, `join`,
`to_upper/lower`, `contains`, `starts_with`, `replace_all`, and
case-insensitive variants (`icontains`, `istarts_with`, …).

`split` writes into any `SequenceContainer<string>` and supports
`token_compress_on` to collapse consecutive delimiters.

---

## `boost/filesystem`

Path manipulation, directory traversal, and file status. The stdlib
`std::filesystem` (C++17) covers most of the same API; Boost.Filesystem
provides a few extras and is available on older toolchains.

Key types: `path`, `directory_iterator`, `recursive_directory_iterator`.
Key free functions: `exists`, `is_directory`, `file_size`, `create_directories`,
`remove_all`, `temp_directory_path`.

---

## `boost/program_options`

Declarative CLI argument parsing for tools and servers. Supports:
- Short (`-p`) and long (`--port`) options.
- Typed values with `po::value<T>()->default_value(v)`.
- Required options — `po::notify()` throws `required_option` if missing.
- `--help` generation from the `options_description`.
- Positional arguments and config-file sources.

---

## `boost/log`

Severity-based, sink-driven logging. Relevant for HPC because:
- Filtering happens at the **source** level — rejected log entries have
  near-zero cost (no string formatting, no I/O).
- Async front-ends decouple the hot path from I/O completely.
- Attribute system allows structured, machine-readable records.

Severity levels (lowest → highest): `trace debug info warning error fatal`.

---

## `boost/asio`

The async I/O engine used in almost every modern C++ network server.
Also available as **standalone Asio** (no Boost dependency) and is the basis
for the `std::net` networking TS proposal.

**Core concepts:**
- `io_context` — the event loop. All async operations post handlers here.
- `steady_timer` — schedule callbacks at a time point.
- Sockets (`tcp::socket`, `udp::socket`) — async read/write.
- `strand<>` — serialise handlers from multiple threads without a mutex.

**Pattern:** every async operation takes a completion handler
`(error_code, bytes_transferred)`. Chain reads and writes by starting the
next operation inside the previous handler. Use `shared_ptr` +
`enable_shared_from_this` to keep session objects alive while operations
are in flight.

---

## `boost/lockfree`

Lock-free data structures — concurrent access without any mutex or CAS spin
at the queue level. Critical for low-latency producer/consumer pipelines.

| Type            | Producers | Consumers | Capacity       |
|-----------------|-----------|-----------|----------------|
| `spsc_queue`    | 1         | 1         | compile-time   |
| `queue`         | N         | N         | fixed or dynamic |
| `stack`         | N         | N         | fixed or dynamic |

`T` must be trivially copyable. Use `capacity<N>` template parameter for
a fixed-size, allocation-free structure.

`consume_all(f)` drains the entire queue in one call — efficient for batched
processing in the consumer thread.

---

## `boost/pool`

Avoid heap fragmentation and amortise `malloc` overhead in tight loops.

| Type                    | Best for                                      |
|-------------------------|-----------------------------------------------|
| `pool<>`                | Raw `void*` chunks of fixed size               |
| `object_pool<T>`        | Typed allocation with constructor/destructor  |
| `pool_allocator<T>`     | STL containers that allocate arrays (vector)  |
| `fast_pool_allocator<T>`| STL containers with single-node allocs (list, map) |

All pools release memory to the OS only on their own destruction — not on
individual `free()` calls. This eliminates per-free OS overhead.

---

## `boost/circular_buffer`

Fixed-capacity ring buffer with O(1) push/pop at both ends and O(1) random
access. When full, `push_back` silently overwrites the oldest element — no
allocation, no reallocation.

HPC use cases:
- **Sliding window statistics**: moving average, rolling min/max over the
  last N samples.
- **Bounded history**: fixed-size event log, audio sample ring.
- **Producer/consumer**: bounded memory regardless of load spikes.

`circular_buffer_space_optimized` is identical but allocates memory lazily.

---

## `boost/signals2`

Thread-safe observer pattern. A `signal<Signature>` holds a list of
connected slots (any callable) and fires them all on invocation.

- `connect()` returns a `connection` handle for later disconnection.
- `scoped_connection` auto-disconnects when it goes out of scope — RAII
  for subscriptions.
- Slots can be given a **group number** to control firing order.
- A **combiner** aggregates return values from all slots.
- Connections can be **temporarily blocked** with `shared_connection_block`
  without being permanently disconnected.
