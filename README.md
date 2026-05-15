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

---

## Setup

```
g++ -std=c++17   # minimum for most files
g++ -std=c++20   # required for concepts, ranges, jthread
```

Each `.cpp` file has its compile command in the first comment line.
