# C++20 Coroutines

## What is a coroutine?

A regular function runs to completion and returns. A coroutine can suspend its
execution mid-way, returning control to the caller, and later be resumed — picking
up exactly where it left off, with all local variables intact.

Three new keywords enable this:
- `co_yield expr` — suspend and produce a value
- `co_await expr` — suspend until an async operation completes
- `co_return expr` — complete the coroutine with a value

A function is a coroutine if its body contains any of these keywords.

---

## Coroutine machinery

Unlike threads, coroutines are stackless: their state lives in a heap-allocated
frame (the compiler generates it). The frame holds local variables, the current
suspension point, and a **promise object**.

### promise_type

Every coroutine type `T` must have a nested `T::promise_type`. The compiler calls
specific methods on the promise to drive the coroutine lifecycle:

```
Coroutine called
  → promise_type constructed
  → get_return_object()        // builds the coroutine object returned to caller
  → co_await initial_suspend() // suspend_always → lazy; suspend_never → eager
  → coroutine body runs
    → co_yield v → yield_value(v) → awaitable → suspend
    → co_await a → awaitable a → may suspend
    → co_return v → return_value(v)
  → co_await final_suspend()   // last chance to transfer control before destroy
  → frame destroyed
```

### coroutine_handle<Promise>

`std::coroutine_handle<P>` is a non-owning pointer to a coroutine frame.
- `.resume()` — resumes the coroutine from its suspension point
- `.done()` — true if the coroutine has reached final_suspend
- `.destroy()` — frees the frame (call once, when done)
- `.promise()` — reference to the promise object
- `::from_promise(p)` — recover handle from promise reference

### Awaitables

Any expression passed to `co_await` must satisfy the *Awaitable* concept:

```cpp
struct MyAwaitable {
    bool await_ready();           // return true → skip suspension entirely
    void await_suspend(coroutine_handle<> h);  // called on suspension; h = current coro
    T    await_resume();          // return value of the co_await expression
};
```

`await_suspend` can return:
- `void` — always suspend, control returns to caller/resumer
- `bool` — false → don't actually suspend (optimisation)
- `coroutine_handle<>` — **symmetric transfer**: resume that handle immediately
  (tail-call; no stack growth; used in Task chains to avoid stack overflow)

Standard awaitables: `std::suspend_always`, `std::suspend_never`.

---

## Key patterns

### Generator<T>
- Uses `co_yield`; `initial_suspend` = `suspend_always` (lazy)
- Caller pulls values one at a time via iterator or `.next()`
- Single-threaded; no concurrency concerns

### Task<T>
- Uses `co_await`; starts lazily (`initial_suspend` = `suspend_always`)
- When a coroutine does `co_await task`, it sets itself as the *continuation*
  and symmetric-transfers into the task
- On completion, `final_suspend` symmetric-transfers back to the continuation
- Exception stored in promise, rethrown in `await_resume`

### Scheduler
- Maintains a queue of `coroutine_handle<>`
- Drives coroutines cooperatively: dequeue, resume, repeat
- Coroutines yield control via a custom `co_await yield_to_scheduler{}`

---

## Symmetric transfer

Without it, deep co_await chains cause unbounded stack growth (each resume adds
a frame). With symmetric transfer, `await_suspend` returns a `coroutine_handle<>`
and the runtime performs a **tail call**: the current frame is removed before the
next one starts.

```cpp
// final_suspend awaiter in Task — resumes continuation without growing the stack
coroutine_handle<> await_suspend(coroutine_handle<promise_type> self) noexcept {
    auto cont = self.promise().continuation;
    return cont ? cont : std::noop_coroutine();
}
```

---

## Coroutines vs threads vs callbacks

| | Coroutines | Threads | Callbacks |
|---|---|---|---|
| Context switch | ~ns (no syscall) | ~µs (syscall) | n/a |
| Stack per unit | none (heap frame) | 8 MB default | n/a |
| Error propagation | exceptions | exceptions | manual |
| Cancellation | stop_token / manual | stop_token | manual |
| Composability | co_await chains | join/future | callback hell |

---

## Compiler support

- GCC 11+ with `-std=c++20`
- Clang 12+ with `-std=c++20`
- MSVC 19.28+ with `/std:c++20`

Headers: `<coroutine>` (C++20 standard)
