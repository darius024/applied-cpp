# Error Handling in C++

C++ offers two complementary error-handling models: **exceptions** for
exceptional conditions and **error values** (`error_code`, `optional`,
`expected`) for expected failures. Choosing the right one depends on
the call site, the severity of the failure, and the performance budget.

---

## Exceptions

An exception is thrown with `throw` and caught with `try/catch`.
The runtime **unwinds the stack** — destructors of all in-scope objects
are called — until a matching handler is found. If none is found,
`std::terminate` is called.

### Catch order matters

Handlers are checked top to bottom. A `catch(std::exception&)` before
`catch(std::runtime_error&)` will shadow the latter. Always catch derived
types before base types.

### Rethrowing

`throw;` (no operand) inside a catch block rethrows the current exception
without slicing it. `throw e;` copies `e` and throws the copy — may lose
the derived type.

---

## The `std::exception` hierarchy

```
std::exception
├── std::logic_error      — programming errors, detectable before runtime
│   ├── std::invalid_argument
│   ├── std::out_of_range
│   └── std::length_error
└── std::runtime_error    — errors only detectable at runtime
    ├── std::overflow_error
    ├── std::underflow_error
    └── std::system_error  (wraps std::error_code)
```

Custom exceptions should inherit from `std::exception` or one of its
subtypes and override `what()`.

---

## `noexcept`

Marks a function as guaranteed not to throw. Violations call `std::terminate`
— no stack unwinding. Used by the standard library to enable optimisations.

**Move constructors and move assignment operators should be `noexcept`.**
The standard library (e.g. `std::vector::resize`) will use the move
constructor only if it is `noexcept`; otherwise it falls back to copy to
preserve the strong exception guarantee.

`noexcept(expr)` evaluates at compile time — true if `expr` is `noexcept`.
Useful for conditional `noexcept` on generic code.

---

## Exception safety guarantees

| Guarantee  | Meaning                                                        |
|------------|----------------------------------------------------------------|
| No-throw   | The function never throws. Invariants always hold.             |
| Strong     | If it throws, the program state is as if the call never happened. |
| Basic      | If it throws, invariants hold but state may have changed.      |
| None       | No guarantee — may leave objects in inconsistent state.        |

The **copy-and-swap idiom** achieves the strong guarantee for assignment:
operate on a copy, then `swap` (which is `noexcept`). If the copy throws,
the original is unchanged.

---

## RAII and exceptions

RAII ensures resources are released even when an exception unwinds the
stack. You do NOT need `try/finally` patterns — just let destructors run.

A **scope guard** generalises this: run arbitrary cleanup on scope exit,
regardless of how the scope exits (return, exception, fall-through).

---

## `std::error_code` and `std::error_condition`

Low-overhead, value-based error reporting without exceptions.

- `error_code` — a specific, implementation-defined error (e.g. OS errno).
- `error_condition` — a portable, abstract condition (e.g. "no such file").
- `error_category` — factory for error codes in a domain (e.g. `generic_category()`).

Comparison: `error_code == errc::no_such_file_or_directory` works across
platforms because the conversion goes through `error_condition`.

---

## `std::optional<T>`

Represents a value that may or may not be present. Use instead of
returning a sentinel (−1, nullptr, "") for functions that can "not find" a result.

```cpp
std::optional<int> find(...)  // has value, or empty
```

Access: `.value()` (throws `bad_optional_access` if empty), `*opt` (UB if empty),
`.value_or(default)` (safe).

---

## `std::variant<T, E>` as an error union

`variant<Result, Error>` carries either a result or an error, determined
at runtime. Accessed with `std::get<T>()` or `std::visit`. More expressive
than a boolean out-parameter.

---

## `std::expected<T, E>` (C++23)

The canonical "result type": either a `T` (success) or an `E` (error),
like `variant<T,E>` but with cleaner semantics and monadic operations:

- `.and_then(f)` — call `f(value)` if success, propagate error otherwise.
- `.transform(f)` — map over the value if success.
- `.or_else(f)` — call `f(error)` if error, propagate value otherwise.

---

## Choosing an error mechanism

| Situation                                        | Use                   |
|--------------------------------------------------|-----------------------|
| Programming error (should never happen)          | `assert` / `throw`    |
| Truly exceptional condition (I/O failure, OOM)   | exception             |
| Expected failure in a hot loop or library API    | `error_code` / `expected` |
| "Nothing found" from a search                    | `optional`            |
| One of several error types possible              | `variant` / `expected`|
| C interop, no exceptions allowed                 | `error_code`          |
