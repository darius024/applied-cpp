// compile: g++ -std=c++20 -O2 -o 03_coroutine_handle 03_coroutine_handle.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <iostream>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// coroutine_handle<Promise> — a raw, non-owning handle to a frame.
//
// This file strips away the Generator/Task wrappers to show exactly
// what the compiler does behind the scenes.
//
// Key operations:
//   handle.resume()     — resume from current suspension point
//   handle.done()       — true after final_suspend
//   handle.destroy()    — free the frame (your responsibility if no RAII wrapper)
//   handle.promise()    — reference to the promise object stored in the frame
//   ::from_promise(p)   — reconstruct handle from a promise reference
//                         (useful inside promise_type methods)
//
// coroutine_handle<void>
//   Type-erased handle — can hold any coroutine handle.
//   Can resume/destroy/done but cannot access the promise.
//   Useful when you don't care about the promise type (e.g. schedulers).
// ─────────────────────────────────────────────────────────────────────

// ── A minimal coroutine that suspends multiple times ─────────────────

struct StepPromise
{
    int step = 0;  // track which step we're at

    // The return type of the coroutine function
    struct Coro
    {
        using promise_type = StepPromise;
        std::coroutine_handle<StepPromise> handle;
    };

    Coro get_return_object() noexcept
    {
        return Coro{std::coroutine_handle<StepPromise>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend()   noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
};

StepPromise::Coro stepping_coroutine()
{
    auto h = std::coroutine_handle<StepPromise>::from_promise(
        co_await std::coroutine_handle<StepPromise>::from_address(nullptr));
    // ── Alternative: access promise via a custom awaitable ────────────
    // (shown below; the line above is illustrative — don't use in prod)

    co_await std::suspend_always{}; // suspend point 1
    co_await std::suspend_always{}; // suspend point 2
    co_await std::suspend_always{}; // suspend point 3
    // coroutine ends → final_suspend → done() becomes true
}

// ── GetPromise awaitable: let a coroutine read its own promise ────────
// The promise is normally opaque inside the coroutine body.
// This trick retrieves it by abusing await_suspend.
template<typename Promise>
struct GetPromise
{
    Promise* promise = nullptr;

    bool await_ready() noexcept { return false; }

    // await_suspend receives the handle of the suspended coroutine;
    // we extract the promise and immediately resume (no actual suspension)
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        promise = &h.promise();
        return false; // false → don't actually suspend
    }

    Promise* await_resume() noexcept { return promise; }
};

struct Counter
{
    int value = 0;

    struct promise_type
    {
        Counter get_return_object() noexcept
        {
            return Counter{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    std::coroutine_handle<promise_type> handle;
};

Counter self_aware_coroutine()
{
    // Retrieve our own promise at runtime
    auto* p = co_await GetPromise<Counter::promise_type>{};
    (void)p; // in practice: use p to read/write state

    co_await std::suspend_always{};
    co_await std::suspend_always{};
}

// ── Symmetric transfer demo ───────────────────────────────────────────
// Transfer execution from one coroutine directly to another.
// Neither stack frame remains live during the transfer — O(1) stack.

struct TransferPromise;
struct TransferCoro
{
    using promise_type = TransferPromise;
    std::coroutine_handle<TransferPromise> handle;
};

struct TransferPromise
{
    std::coroutine_handle<> next; // where to transfer on completion

    TransferCoro get_return_object() noexcept
    {
        return TransferCoro{std::coroutine_handle<TransferPromise>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    // On completion: tail-call to `next` if set, otherwise noop
    struct FinalAwaiter
    {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<TransferPromise> h) noexcept
        {
            auto n = h.promise().next;
            return n ? n : std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
};

TransferCoro worker(std::string name)
{
    std::cout << "  [" << name << "] running\n";
    co_return;
}

int main()
{
    // ── Basic handle operations ───────────────────────────────────────
    std::cout << "=== basic handle ===\n";
    {
        auto coro = self_aware_coroutine();
        auto h    = coro.handle;

        std::cout << "done before any resume: " << std::boolalpha << h.done() << "\n";

        h.resume(); // initial_suspend → first explicit suspend_always
        std::cout << "done after resume 1: " << h.done() << "\n";

        h.resume(); // second suspend_always
        std::cout << "done after resume 2: " << h.done() << "\n";

        h.resume(); // runs to completion → final_suspend
        std::cout << "done after resume 3: " << h.done() << "\n";

        h.destroy(); // must destroy manually (no RAII wrapper here)
    }

    // ── Type-erased coroutine_handle<void> ────────────────────────────
    std::cout << "\n=== type-erased handle ===\n";
    {
        auto coro = self_aware_coroutine();
        // Implicit conversion to void handle
        std::coroutine_handle<> erased = coro.handle;

        erased.resume();
        erased.resume();
        erased.resume();
        std::cout << "erased.done(): " << erased.done() << "\n";
        erased.destroy();
    }

    // ── Symmetric transfer between two coroutines ─────────────────────
    std::cout << "\n=== symmetric transfer ===\n";
    {
        auto b = worker("B");
        auto a = worker("A");

        // Chain: when A finishes, transfer to B
        a.handle.promise().next = b.handle;

        std::cout << "resuming A\n";
        a.handle.resume(); // A runs, completes → tail-calls B → B runs

        // Both done after one resume of A
        std::cout << "A.done: " << a.handle.done()
                  << " B.done: " << b.handle.done() << "\n";

        a.handle.destroy();
        b.handle.destroy();
    }
}
