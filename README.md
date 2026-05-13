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

---

## Setup

```
g++ -std=c++17   # minimum for most files
g++ -std=c++20   # required for concepts, ranges, jthread
```

Each `.cpp` file has its compile command in the first comment line.
