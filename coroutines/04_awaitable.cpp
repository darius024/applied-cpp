// compile: g++ -std=c++20 -O2 -o 04_awaitable 04_awaitable.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <iostream>
#include <functional>
#include <queue>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Custom awaitables — the three-method protocol.
//
// When the compiler sees `co_await expr`:
//   1. Resolve the awaitable (operator co_await or use expr directly).
//   2. Call awaitable.await_ready().
//      → true: skip suspension entirely, go directly to await_resume().
//      → false: call await_suspend(current_handle), then:
//         void return  → suspend; control returns to caller/resumer.
//         bool return  → false → don't suspend (useful for fast-path).
//         handle return → symmetric transfer to that coroutine.
//   3. When later resumed, call awaitable.await_resume().
//      Its return value is the result of the co_await expression.
//
// This file shows four practical awaitable patterns:
//   ImmediateValue   — never suspends; result always ready
//   AlwaysSuspend    — always suspends; manual resume required
//   ReadyWhen        — suspends only if condition not yet met
//   Deferred         — captures a callback, runs it on resume
// ─────────────────────────────────────────────────────────────────────

// Minimal Task<T> for running examples (same as 02_task.cpp)
template<typename T>
class Task
{
public:
    struct promise_type
    {
        T result{};
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept
            {
                auto c = h.promise().continuation;
                return c ? c : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_value(T v) { result = std::move(v); }
        void unhandled_exception() {}
    };

    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() noexcept { return false; }
    handle_type await_suspend(std::coroutine_handle<> c) noexcept
    {
        handle_.promise().continuation = c;
        return handle_;
    }
    T await_resume() { return std::move(handle_.promise().result); }
    T sync_wait() { handle_.resume(); return await_resume(); }

    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Task() { if (handle_) handle_.destroy(); }
    Task(const Task&) = delete;

private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// 1. ImmediateValue<T>
//    await_ready() = true → body of await_resume() is the result.
//    No suspension occurs; the coroutine continues on the same thread.
// ─────────────────────────────────────────────────────────────────────
template<typename T>
struct ImmediateValue
{
    T value;

    bool await_ready() noexcept { return true; } // never suspend

    void await_suspend(std::coroutine_handle<>) noexcept {}

    T await_resume() noexcept { return std::move(value); }
};

// ─────────────────────────────────────────────────────────────────────
// 2. AlwaysSuspend
//    Suspends the coroutine unconditionally; stashes the handle in the
//    provided slot so the caller can resume it later.
//    Models a "park until externally woken" pattern.
// ─────────────────────────────────────────────────────────────────────
struct AlwaysSuspend
{
    std::coroutine_handle<>& slot; // out-param: receives the suspended handle

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        slot = h; // hand the handle to whoever will resume us
    }

    void await_resume() noexcept {}
};

// ─────────────────────────────────────────────────────────────────────
// 3. ReadyWhen
//    Suspends only if a condition function returns false.
//    If the condition is already true, execution continues inline.
//    Used for polling patterns: co_await ReadyWhen{[&]{ return ready; }};
// ─────────────────────────────────────────────────────────────────────
struct ReadyWhen
{
    std::function<bool()>   condition;
    std::coroutine_handle<>& slot;
    std::string              name; // for tracing

    bool await_ready() noexcept
    {
        bool ready = condition();
        std::cout << "  [ReadyWhen:" << name << "] await_ready=" << ready << "\n";
        return ready;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        std::cout << "  [ReadyWhen:" << name << "] suspending\n";
        slot = h;
    }

    void await_resume() noexcept
    {
        std::cout << "  [ReadyWhen:" << name << "] resumed\n";
    }
};

// ─────────────────────────────────────────────────────────────────────
// 4. Deferred
//    Registers a callback that is invoked on suspension.
//    The callback receives the coroutine handle and may resume it
//    synchronously or stash it for later.
//    This is how Asio's use_awaitable token works internally.
// ─────────────────────────────────────────────────────────────────────
struct Deferred
{
    std::function<void(std::coroutine_handle<>)> on_suspend;
    int result_value = 0;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        on_suspend(h); // delegate to the registered callback
    }

    int await_resume() noexcept { return result_value; }
};

// ─────────────────────────────────────────────────────────────────────
// Demo
// ─────────────────────────────────────────────────────────────────────

std::coroutine_handle<> parked; // global slot for AlwaysSuspend / ReadyWhen

Task<int> demo_immediate()
{
    std::cout << "\n--- ImmediateValue ---\n";
    int v = co_await ImmediateValue<int>{42};
    std::cout << "result (no suspension): " << v << "\n";
    co_return v;
}

Task<int> demo_always_suspend()
{
    std::cout << "\n--- AlwaysSuspend ---\n";
    std::cout << "before suspend\n";
    co_await AlwaysSuspend{parked};
    std::cout << "after resume\n"; // printed only after parked.resume()
    co_return 1;
}

bool flag = false; // will be set to true by external code

Task<int> demo_ready_when()
{
    std::cout << "\n--- ReadyWhen ---\n";
    co_await ReadyWhen{[&]{ return flag; }, parked, "flag"};
    std::cout << "condition was satisfied\n";
    co_return 2;
}

Task<int> demo_deferred()
{
    std::cout << "\n--- Deferred ---\n";
    // The lambda receives our handle; we immediately resume with a value
    auto awaitable = Deferred{
        [](std::coroutine_handle<> h) {
            std::cout << "callback: setting result and resuming\n";
            // In a real async system: post this to an I/O thread
            // Here: resume synchronously for demo purposes
            h.resume();
        },
        99
    };
    int v = co_await awaitable;
    std::cout << "deferred result: " << v << "\n";
    co_return v;
}

int main()
{
    // ImmediateValue: no suspension
    demo_immediate().sync_wait();

    // AlwaysSuspend: manual external resume
    {
        auto t = demo_always_suspend();
        t.sync_wait(); // runs to first suspend_always (initial), then resumes
        // t is now at the AlwaysSuspend; parked holds the handle
        if (parked) {
            std::cout << "externally resuming parked coroutine\n";
            parked.resume();
            parked = nullptr;
        }
        // Note: task already finished; avoid double-free by not calling sync_wait again
    }

    // ReadyWhen: condition false → suspends; condition true → doesn't
    {
        flag = false;
        auto t = demo_ready_when();
        t.sync_wait(); // suspends at ReadyWhen (flag is false)
        std::cout << "setting flag=true and resuming\n";
        flag = true;
        if (parked) { parked.resume(); parked = nullptr; }
    }

    // Deferred: callback pattern
    demo_deferred().sync_wait();
}
