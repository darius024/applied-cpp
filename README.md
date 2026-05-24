# applied-cpp

Production C++ patterns and practices, organised by topic.
Each topic folder contains a `doc.md` with theory plus numbered `.cpp` examples.

---

## Topics

### `raii`
Resource Acquisition Is Initialisation: deterministic ownership, smart pointers,
custom deleters, rule of zero/five.

| File | Topic |
|------|-------|
| `01_memory.cpp` | `unique_ptr`, `shared_ptr`, `weak_ptr`, `make_unique`/`make_shared` |
| `02_file.cpp` | RAII wrapper around `FILE*` / file descriptors |
| `03_move.cpp` | Move semantics, rule of five, transferring ownership |
| `04_lock.cpp` | `lock_guard`, `unique_lock`, `scoped_lock` as RAII over a mutex |
| `05_custom_deleter.cpp` | Custom deleters for C handles and resources |
| `06_rule_of_zero.cpp` | Rely on members' own RAII so the class needs no special members |

### `oop`
Object-oriented design in modern C++: virtual dispatch, interfaces, CRTP,
factory patterns.

| File | Topic |
|------|-------|
| `01_interfaces.cpp` | Pure-virtual interfaces, abstract base classes |
| `02_dynamic_binding.cpp` | Virtual dispatch, vtable cost, slicing |
| `03_inheritance_vs_composition.cpp` | When to inherit vs hold a member |
| `04_operator_overloading.cpp` | `operator+`, `operator==`, spaceship, free vs member |
| `05_copy_and_swap.cpp` | Strong exception safety via copy-and-swap |
| `06_nvi.cpp` | Non-Virtual Interface idiom: public non-virtual, private virtual |
| `07_override_final.cpp` | `override` and `final`, devirtualisation |
| `08_factory.cpp` | Factory functions returning `unique_ptr` to interfaces |
| `09_crtp.cpp` | Curiously Recurring Template Pattern, static polymorphism |

### `templates`
Template metaprogramming: function and class templates, SFINAE, `if constexpr`,
concepts, type traits.

| File | Topic |
|------|-------|
| `01_function_class_templates.cpp` | Basic function and class templates |
| `02_specialisation.cpp` | Full and partial template specialisation |
| `03_variadic.cpp` | Variadic templates, parameter packs, fold expressions |
| `04_sfinae.cpp` | `enable_if`, `void_t`, detection idiom |
| `05_if_constexpr.cpp` | C++17 compile-time branching inside templates |
| `06_policy_classes.cpp` | Behaviour parameterised via template-template parameters |
| `07_concepts_basics.cpp` | Defining and applying concepts, overload subsumption |
| `08_standard_concepts.cpp` | `integral`, `regular`, `invocable`, range concepts |
| `09_type_traits.cpp` | `is_same`, `remove_reference`, `conditional`, building traits |
| `10_metaprogramming.cpp` | Compile-time recursion, type lists, tag dispatch |
| `11_decltype_auto.cpp` | `decltype`, `decltype(auto)`, `declval`, trailing returns |

### `lambdas`
Lambdas and callable objects: captures, generic lambdas, callbacks, higher-order use.

| File | Topic |
|------|-------|
| `01_syntax_captures.cpp` | Capture by value, by reference, init capture, mutable |
| `02_generic_templated.cpp` | `auto` parameters, templated lambdas (C++20) |
| `03_function_vs_fp_vs_template.cpp` | `std::function` vs function pointer vs template callable |
| `04_iile.cpp` | Immediately Invoked Lambda Expression for one-shot init |
| `05_callbacks.cpp` | Non-owning vs owning callbacks, event emitter pattern |
| `06_higher_order.cpp` | Returning lambdas, composing callables, currying |

### `concurrency`
Threads, locks, atomics, futures, thread pools, timers.

| File | Topic |
|------|-------|
| `01_thread_basics.cpp` | `std::thread`, `jthread`, `join`/`detach`, stop tokens |
| `02_mutex_locks.cpp` | `mutex`, `shared_mutex`, `lock_guard`, `scoped_lock` |
| `03_condition_variable.cpp` | `condition_variable`, wait/notify, spurious wakeups |
| `04_atomic.cpp` | `std::atomic`, memory orders, compare-exchange |
| `05_data_races.cpp` | What constitutes a race, fixes with mutex or atomic |
| `06_async_future.cpp` | `std::async`, `future`, `promise`, deferred vs async launch |
| `07_async_handlers.cpp` | `packaged_task`, chaining work, continuation patterns |
| `08_thread_pool.cpp` | Hand-rolled thread pool with a task queue |
| `09_timers.cpp` | `steady_clock`, sleep, scheduled work |

### `errors`
Exceptions, `noexcept`, `optional`, `variant`, `expected`, error codes.

| File | Topic |
|------|-------|
| `01_exceptions_basics.cpp` | `throw`/`catch`, stack unwinding, what `std::exception` provides |
| `02_exception_types.cpp` | Standard exception hierarchy, defining custom types |
| `03_noexcept.cpp` | `noexcept` specifier and operator, effect on move semantics |
| `04_exception_safety.cpp` | Basic, strong, and nothrow guarantees |
| `05_raii_exceptions.cpp` | RAII guards that survive exceptions cleanly |
| `06_error_codes.cpp` | `std::error_code`, `error_category`, system_error |
| `07_optional_variant.cpp` | `optional` for absence, `variant` for tagged unions |
| `08_expected.cpp` | C++23 `std::expected`, monadic chaining |
| `09_patterns.cpp` | Choosing between exceptions, codes, optional, expected |

### `algorithms`
Standard library algorithms: sorting, searching, partitioning, numeric, ranges.

| File | Topic |
|------|-------|
| `01_sorting.cpp` | `sort`, `stable_sort`, `partial_sort`, `nth_element` |
| `02_searching.cpp` | Linear search: `find`, `find_if`, `search`, `adjacent_find` |
| `03_binary_search.cpp` | `binary_search`, `lower_bound`, `upper_bound`, `equal_range` |
| `04_partition.cpp` | `partition`, `stable_partition`, `partition_point` |
| `05_remove_erase.cpp` | Erase-remove idiom, `unique`, C++20 `std::erase` |
| `06_transform.cpp` | `transform`, `for_each`, projections |
| `07_numeric.cpp` | `accumulate`, `reduce`, `inner_product`, `partial_sum`, `iota` |
| `08_predicates.cpp` | `all_of`, `any_of`, `none_of`, `count_if` |
| `09_minmax_heap.cpp` | `min`/`max`/`minmax`, heap algorithms |
| `10_set_permutations.cpp` | Sorted-set operations, `next_permutation` |
| `11_ranges.cpp` | C++20 ranges and views, pipe composition |

### `networking`
POSIX sockets, TCP/UDP patterns, multiplexing, zero-copy, `io_uring`.

| File | Topic |
|------|-------|
| `01_tcp_server_client.cpp` | Blocking TCP server and client, `accept` loop |
| `02_tcp_options.cpp` | `SO_REUSEADDR`, `TCP_NODELAY`, send/recv buffers |
| `03_nonblocking_io.cpp` | `O_NONBLOCK`, `EAGAIN`/`EWOULDBLOCK` handling |
| `04_udp_basics.cpp` | `sendto`/`recvfrom`, datagram boundaries |
| `05_udp_multicast.cpp` | `IP_ADD_MEMBERSHIP`, multicast send and receive |
| `06_select_poll_epoll.cpp` | Comparing `select`, `poll`, and `epoll` |
| `07_unix_domain_sockets.cpp` | `AF_UNIX` sockets, fd passing |
| `08_sendfile_zerocopy.cpp` | `sendfile`, `splice`, kernel-side data movement |
| `09_io_uring.cpp` | `io_uring` submission and completion queues (Linux) |
| `10_socket_patterns.cpp` | Reactor and proactor patterns, accept thread plus workers |

### `system_calls`
POSIX system calls in modern C++: file I/O, mmap, IPC, signals, multiplexing.

| File | Topic |
|------|-------|
| `01_file_io.cpp` | `open`/`read`/`write`/`close`, RAII fd wrapper |
| `02_stat_links.cpp` | `stat`/`fstat`, hard links, symlinks, `readlink` |
| `03_mmap_file.cpp` | `mmap` of a file: read-only and copy-on-write |
| `04_mmap_ipc.cpp` | Shared memory IPC with `mmap` plus semaphore |
| `05_fork_exec.cpp` | `fork`/`execve`/`waitpid`, pipes across `fork` |
| `06_signals.cpp` | `sigaction`, `signalfd`, async-signal safety |
| `07_pipes_fifo.cpp` | Anonymous pipes and named FIFOs |
| `08_poll.cpp` | `poll`/`ppoll` over many file descriptors |
| `09_file_locking.cpp` | `fcntl` advisory locks, `flock` |
| `10_epoll_kqueue.cpp` | `epoll` (Linux) and `kqueue` (macOS) edge-triggered loops |

### `performance`
Low-level performance: cache, layout, branches, SIMD, allocators, benchmarking.

| File | Topic |
|------|-------|
| `01_cache_effects.cpp` | Sequential vs random access, false sharing, AoS vs SoA |
| `02_memory_layout.cpp` | Struct padding, `alignas`, cache-line alignment, packed structs |
| `03_branch_prediction.cpp` | Sorted vs unsorted branches, branchless idioms, `[[likely]]` |
| `04_simd_vectorization.cpp` | `__restrict__`, `assume_aligned`, auto-vectorised loops |
| `05_compiler_hints.cpp` | `always_inline`, `hot`/`cold`, `__builtin_unreachable`, `pure`/`const` |
| `06_latency_timing.cpp` | `clock_gettime`, RAII timer, rdtsc, percentile histograms |
| `07_allocators.cpp` | `pmr::monotonic_buffer_resource`, pool resource, custom arena |
| `08_lockfree.cpp` | Acquire/release, CAS loops, SPSC queue, relaxed counters |
| `09_benchmarking.cpp` | `DoNotOptimize`, `ClobberMemory`, warmup, timer overhead |
| `10_profiling.cpp` | RAII probes, ring-buffer histograms, `perf_event_open` |

### `build_tooling`
Compiler flags, sanitizers, static analysis, build modes, PCH, IWYU, ccache.

| File | Topic |
|------|-------|
| `CMakeLists.txt` | Modern CMake: INTERFACE targets, sanitizer options, clang-tidy, LTO, CTest |
| `01_asan_heap.cpp` | AddressSanitizer: heap overflow, use-after-free, iterator invalidation |
| `02_asan_stack_leak.cpp` | ASan: stack overflow, use-after-scope, use-after-return, LeakSanitizer |
| `03_ubsan.cpp` | UndefinedBehaviorSanitizer: overflow, null deref, misalignment, bad shifts |
| `04_tsan.cpp` | ThreadSanitizer: data races, lock-order inversion, `scoped_lock` fix |
| `05_msan.cpp` | MemorySanitizer: uninitialised reads, taint propagation (Clang on Linux) |
| `06_compiler_warnings.cpp` | `-Wall`, `-Wextra`, `-Wshadow`, `-Wconversion`, `-Wformat=2` |
| `07_clang_tidy.cpp` | `modernize-*`, `bugprone-*`, `readability-*`, `performance-*` checks |
| `08_build_modes.cpp` | Debug/Release/RelWithDebInfo, `NDEBUG`, `assert`, LTO impact |
| `09_pch_unity.cpp` | Precompiled headers, unity builds, `compile_commands.json`, IWYU |
| `10_toolchain.cpp` | ccache, linker choice (`bfd`, `gold`, `lld`, `mold`), dead-stripping |

### `coroutines`
C++20 coroutines: `co_await`, `co_yield`, `co_return`, awaitables, schedulers.

| File | Topic |
|------|-------|
| `01_generator.cpp` | `co_yield` generator: lazy sequence, fibonacci |
| `02_task.cpp` | `co_return` task: async result type, chained awaits |
| `03_coroutine_handle.cpp` | Raw `coroutine_handle<>`: manual resume/destroy |
| `04_awaitable.cpp` | Custom awaitable: `await_ready`/`suspend`/`resume` |
| `05_thread_hop.cpp` | Suspend on one thread, resume on another |
| `06_scheduler.cpp` | Simple coroutine scheduler with a ready queue |
| `07_pipeline.cpp` | Producer, transform, and consumer coroutine stages |
| `08_asio_coro.cpp` | Asio-backed coroutines: `co_spawn`, `async_read`/`async_write` |
| `09_when_all.cpp` | `when_all`: fan-out and collect results |
| `10_cancellation.cpp` | Stop tokens, cooperative cancellation |

### `grpc_proto`
Protocol Buffers and gRPC with CMake. Covers proto3, arena allocation, all four
RPC streaming types, async completion queues, interceptors, deadlines.

| File | Topic |
|------|-------|
| `protos/` | `person.proto`, `advanced.proto`, `metrics.proto` |
| `01_proto_basics.cpp` | Accessors, nested messages, repeated fields, serialisation |
| `02_proto_advanced.cpp` | Maps, oneof, Any, Duration, reflection API |
| `03_proto_arena.cpp` | Arena allocation: bump allocator, `Reset`, `ArenaOptions` |
| `04_grpc_unary.cpp` | Sync unary RPC: `ServerBuilder`, stub, `Status` |
| `05_grpc_server_stream.cpp` | Server-side streaming: `ServerWriter`, `ClientReader` |
| `06_grpc_client_stream.cpp` | Client-side streaming: `ClientWriter`, `WritesDone`, `Finish` |
| `07_grpc_bidi_stream.cpp` | Bidirectional streaming: `ServerReaderWriter`, split-thread client |
| `08_grpc_async.cpp` | Async server: `CompletionQueue`, `CallData` state machine |
| `09_grpc_interceptors.cpp` | Server and client interceptors: logging, auth injection |
| `10_grpc_deadline_cancel.cpp` | Deadlines, `DEADLINE_EXCEEDED`, `TryCancel`, `IsCancelled` |

### `testing`
GoogleTest, GMock, and Catch2: fixtures, parameterised tests, death tests, BDD.

| File | Topic |
|------|-------|
| `01_gtest_basics.cpp` | `TEST`, `EXPECT_*`, `ASSERT_*`, custom messages |
| `02_gtest_fixtures.cpp` | `TEST_F`, `SetUp`/`TearDown`, shared fixture state |
| `03_gtest_parameterized.cpp` | `TEST_P`, `INSTANTIATE_TEST_SUITE_P`, value and type parameters |
| `04_gtest_death_tests.cpp` | `EXPECT_DEATH`, `EXPECT_EXIT`, death test styles |
| `05_gmock_basics.cpp` | `MOCK_METHOD`, `EXPECT_CALL`, `ON_CALL`, call counts |
| `06_gmock_matchers.cpp` | `Eq`, `Ne`, `HasSubstr`, `ElementsAre`, `Field`, custom matchers |
| `07_gmock_actions.cpp` | `Return`, `DoAll`, `Invoke`, `SaveArg`, `SetArgPointee` |
| `08_catch2_basics.cpp` | `TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`, `Approx` |
| `09_catch2_bdd.cpp` | `SCENARIO`, `GIVEN`/`WHEN`/`THEN` BDD style |
| `10_testability_patterns.cpp` | Dependency injection, seams, fakes, stubs, test doubles |

### `libraries`
Third-party libraries, each in its own subfolder with `doc.md` and numbered examples.

| Subfolder | Library | Focus |
|-----------|---------|-------|
| `boost/` | Boost | `string_algo`, `filesystem`, `program_options`, `log`, `asio`, `lockfree`, `pool`, `circular_buffer`, `signals2` |
| `folly/` | Meta Folly | `fbvector`, F14 hash maps, lock-free queues, `Synchronized`, `IOBuf`, futures, executors, `small_vector`, arena, `SharedMutex` |
| `abseil/` | Google Abseil | `flat_hash_map`, btree containers, `InlinedVector`, `Span`, `Status`/`StatusOr`, string utils, `Time`/`Duration`, `Mutex` + annotations, sync primitives, `FunctionRef`/`AnyInvocable` |
| `serialisation/` | FlatBuffers and Cap'n Proto | FlexBuffers (schemaless), schema-based FlatBuffers, Cap'n Proto zero-copy wire format |

---

## Setup

```sh
g++ -std=c++17   # minimum for most files
g++ -std=c++20   # required for concepts, ranges, jthread, coroutines
g++ -std=c++23   # required for std::expected and a few examples
```

Each `.cpp` file has its compile command in the top comment.
