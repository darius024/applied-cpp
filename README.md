# applied-cpp

Production C++ patterns and practices, organised by topic branch.
Each branch contains a `doc.md` with theory and numbered `.cpp` examples.

---

## Branches

### `raii`
Resource Acquisition Is Initialisation — deterministic ownership, custom deleters,
`unique_ptr` / `shared_ptr` / `weak_ptr`, rule of five, `std::optional` as a nullable owner.

### `oop`
Object-oriented design in modern C++ — virtual dispatch, abstract interfaces, CRTP,
mixins, type erasure (`std::any`, `std::variant`, manual vtable), pImpl idiom.

### `templates`
Template metaprogramming — function/class templates, specialisation, variadic templates,
fold expressions, SFINAE, `if constexpr`, concepts (C++20), policy classes.

### `lambdas`
Lambdas and callable objects — capture modes, generic lambdas, immediately invoked
lambdas, recursive lambdas, `std::function`, composing callables.

### `concurrency`
Concurrent programming — `std::thread`, `jthread`, mutexes, condition variables,
`std::atomic`, memory order, `std::async` / `std::future`, thread pools.

### `errors`
Error handling — exceptions, `noexcept`, `std::expected` (C++23), error codes,
`std::error_code` / `std::error_category`, RAII guards, `std::terminate`.

### `algorithms`
Standard library algorithms — sorting, searching, partitioning, numeric algorithms,
ranges (C++20), views, custom comparators, algorithm composition.

### `libraries`
Production third-party libraries, each in its own subfolder with `doc.md` and
numbered examples focused on high-performance systems use cases.

| Subfolder | Library | Focus |
|-----------|---------|-------|
| `boost/` | Boost | string_algo, filesystem, program_options, log, asio, lockfree, pool, circular_buffer, signals2 |
| `folly/` | Meta Folly | fbvector, F14 hash maps, lock-free queues, Synchronized, IOBuf, futures, executors, small_vector, arena, SharedMutex |
| `abseil/` | Google Abseil | flat_hash_map, btree containers, InlinedVector, Span, Status/StatusOr, string utils, Time/Duration, Mutex+annotations, Notification/Barrier/BlockingCounter, FunctionRef/AnyInvocable |
| `serialisation/` | FlatBuffers & Cap'n Proto | FlexBuffers (schemaless), schema-based FlatBuffers, Cap'n Proto zero-copy wire format |

### `grpc_proto`
Protocol Buffers and gRPC for high-performance RPC. CMake-based build with generated
stubs. Covers proto3 syntax, arena allocation, all four RPC streaming types (unary,
server-stream, client-stream, bidi), async CQ server, interceptors, deadlines, and cancellation.

| File | Topic |
|------|-------|
| `protos/` | `person.proto`, `advanced.proto`, `metrics.proto` |
| `01_proto_basics.cpp` | Accessors, nested messages, repeated fields, serialisation |
| `02_proto_advanced.cpp` | Maps, oneof, Any, Duration, Reflection API |
| `03_proto_arena.cpp` | Arena allocation — bump allocator, Reset, ArenaOptions |
| `04_grpc_unary.cpp` | Sync unary RPC — ServerBuilder, stub, Status |
| `05_grpc_server_stream.cpp` | Server-side streaming — ServerWriter, ClientReader |
| `06_grpc_client_stream.cpp` | Client-side streaming — ClientWriter, WritesDone, Finish |
| `07_grpc_bidi_stream.cpp` | Bidirectional streaming — ServerReaderWriter, split-thread client |
| `08_grpc_async.cpp` | Async server — CompletionQueue, CallData state machine |
| `09_grpc_interceptors.cpp` | Server + client interceptors — logging, auth token injection |
| `10_grpc_deadline_cancel.cpp` | Deadlines, DEADLINE_EXCEEDED, TryCancel, IsCancelled |

### `coroutines`
C++20 coroutines from first principles — `co_await`, `co_yield`, `co_return`, promise
types, awaitable protocol, coroutine handles, and practical patterns: generators,
async tasks, thread-hopping, pipelines, schedulers, `when_all`, and cancellation.
Includes an `asio`-backed coroutine example.

| File | Topic |
|------|-------|
| `01_generator.cpp` | `co_yield` generator — lazy sequence, fibonacci, range adaptor |
| `02_task.cpp` | `co_return` task — async result type, chained awaits |
| `03_coroutine_handle.cpp` | Raw `coroutine_handle<>` — manual resume/destroy, frame lifetime |
| `04_awaitable.cpp` | Custom awaitable — `await_ready/suspend/resume` protocol |
| `05_thread_hop.cpp` | Thread-hopping — suspend on one thread, resume on another |
| `06_scheduler.cpp` | Simple coroutine scheduler — round-robin, ready queue |
| `07_pipeline.cpp` | Coroutine pipeline — producer → transform → consumer stages |
| `08_asio_coro.cpp` | Asio-backed coroutines — `co_spawn`, `async_read/write` |
| `09_when_all.cpp` | `when_all` — fan-out, collect all results |
| `10_cancellation.cpp` | Cancellation — stop token, cooperative cancellation |

### `testing`
Automated testing with GoogleTest and Catch2 — test fixtures, parameterised tests,
death tests, mocking with GMock (matchers, actions, expectations), Catch2 BDD style,
and testability patterns (dependency injection, seams, test doubles).

| File | Topic |
|------|-------|
| `01_gtest_basics.cpp` | `TEST`, `EXPECT_*`, `ASSERT_*`, custom messages |
| `02_gtest_fixtures.cpp` | `TEST_F`, `SetUp`/`TearDown`, shared fixture state |
| `03_gtest_parameterized.cpp` | `TEST_P`, `INSTANTIATE_TEST_SUITE_P`, value/type parameters |
| `04_gtest_death_tests.cpp` | `EXPECT_DEATH`, `EXPECT_EXIT`, death test styles |
| `05_gmock_basics.cpp` | `MOCK_METHOD`, `EXPECT_CALL`, `ON_CALL`, call counts |
| `06_gmock_matchers.cpp` | `Eq`, `Ne`, `HasSubstr`, `ElementsAre`, `Field`, custom matchers |
| `07_gmock_actions.cpp` | `Return`, `DoAll`, `Invoke`, `SaveArg`, `SetArgPointee` |
| `08_catch2_basics.cpp` | `TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`, Approx |
| `09_catch2_bdd.cpp` | `SCENARIO`, `GIVEN`/`WHEN`/`THEN` BDD style |
| `10_testability_patterns.cpp` | Dependency injection, seams, fakes, stubs, test doubles |

### `system_calls`
POSIX system calls in modern C++ — file I/O, memory-mapped files, IPC, process
management, signals, pipes, poll/epoll/kqueue, and file locking. Each example wraps
syscalls in RAII and uses `std::error_code` for error handling.

| File | Topic |
|------|-------|
| `01_file_io.cpp` | `open`/`read`/`write`/`close`, O_flags, RAII fd wrapper |
| `02_stat_links.cpp` | `stat`/`fstat`, hard links, symlinks, `readlink` |
| `03_mmap_file.cpp` | `mmap` file — read-only and copy-on-write mappings |
| `04_mmap_ipc.cpp` | `mmap` shared memory IPC — producer/consumer with semaphore |
| `05_fork_exec.cpp` | `fork`/`execve`, `waitpid`, environment, pipe before fork |
| `06_signals.cpp` | `sigaction`, `signalfd`, `sigprocmask`, async-signal safety |
| `07_pipes_fifo.cpp` | Anonymous pipes, named FIFOs, non-blocking I/O |
| `08_poll.cpp` | `poll`/`ppoll` — multiplexing multiple fds |
| `09_file_locking.cpp` | `fcntl` advisory locks, `flock`, lock contention |
| `10_epoll_kqueue.cpp` | `epoll` (Linux) / `kqueue` (macOS) — edge-triggered event loop |

### `performance`
Low-level performance engineering — CPU cache effects, memory layout, branch
prediction, SIMD auto-vectorisation, compiler hints, latency measurement, custom
allocators, lock-free data structures, benchmarking methodology, and profiling.

| File | Topic |
|------|-------|
| `01_cache_effects.cpp` | Sequential vs random access, false sharing, AoS vs SoA |
| `02_memory_layout.cpp` | Struct padding, `alignas`, cache-line aligned structs |
| `03_branch_prediction.cpp` | Sorted vs unsorted, branchless abs/clamp, `[[likely]]` |
| `04_simd_vectorization.cpp` | `__restrict__`, `assume_aligned`, vectorisable VWAP |
| `05_compiler_hints.cpp` | `always_inline`, `hot`/`cold`, `__builtin_unreachable` |
| `06_latency_timing.cpp` | `clock_gettime`, RAII timer, rdtsc, percentile histograms |
| `07_allocators.cpp` | `pmr` monotonic/pool resource, custom arena vs `new/delete` |
| `08_lockfree.cpp` | Acquire/release, CAS loop, SPSC queue, relaxed counters |
| `09_benchmarking.cpp` | `DoNotOptimize`, `ClobberMemory`, warmup, timer overhead |
| `10_profiling.cpp` | RAII probes, ring-buffer histogram, `perf_event_open`, tool guide |

### `build_tooling`
CMake, Ninja, sanitizers, static analysis, and the broader C++ toolchain.
Covers the full build pipeline from source to binary — warning flags, optimisation
levels, LTO, PCH, unity builds, ccache, linker selection, and cross-compilation.

| File | Topic |
|------|-------|
| `CMakeLists.txt` | Modern CMake — `INTERFACE` targets, sanitizer options, clang-tidy, LTO, CTest |
| `01_asan_heap.cpp` | ASan: heap overflow, use-after-free, iterator invalidation |
| `02_asan_stack_leak.cpp` | ASan: stack overflow, use-after-scope, use-after-return, LSan |
| `03_ubsan.cpp` | UBSan: signed overflow, null deref, misaligned, shift, enum cast |
| `04_tsan.cpp` | TSan: data races, lock-order inversion, `std::scoped_lock` fix |
| `05_msan.cpp` | MSan: uninitialised reads, taint propagation (Clang/Linux) |
| `06_compiler_warnings.cpp` | `-Wall`/`-Wextra`/`-Wshadow`/`-Wconversion`/`-Wformat=2` patterns |
| `07_clang_tidy.cpp` | `modernize-*`, `bugprone-*`, `readability-*`, `performance-*` |
| `08_build_modes.cpp` | `Debug`/`Release`/`RelWithDebInfo`, `NDEBUG`, `assert`, LTO |
| `09_pch_unity.cpp` | Precompiled headers, unity builds, `compile_commands.json`, IWYU |
| `10_toolchain.cpp` | ccache, linker benchmarks (`bfd`→`mold`), dead-strip, cross-compile |

---

## Setup

```
g++ -std=c++17   # minimum for most files
g++ -std=c++20   # required for concepts, ranges, jthread
```

Each `.cpp` file has its compile command in the first comment line.
