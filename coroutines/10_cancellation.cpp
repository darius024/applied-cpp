// compile: g++ -std=c++20 -O2 -pthread -o 10_cancellation 10_cancellation.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <stop_token>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Coroutine cancellation via std::stop_token (C++20).
//
// stop_source: owns the cancellation state; call request_stop() to cancel.
// stop_token:  non-owning view; poll with stop_requested() or register
//              a stop_callback for notification.
// stop_callback: RAII object; fires its callable when stop is requested
//   (or immediately if already requested when constructed).
//
// Patterns shown:
//   1. Cancellable generator — checks stop_token between yields.
//   2. Cancellable Task — checks at await points; stop_callback wakes it.
//   3. CancellableAwaiter — integrates stop_token into a custom awaitable
//      so any co_await can be interrupted.
//   4. jthread integration — stop_token from jthread's built-in source.
//
// Key rule: coroutines cannot be forcibly killed from the outside —
// they must cooperate by checking the token at safe points.
// ─────────────────────────────────────────────────────────────────────

// ── Minimal Generator from 01_generator.cpp ──────────────────────────
template<typename T>
class Generator
{
public:
    struct promise_type
    {
        T current{};
        Generator get_return_object() noexcept
        { return Generator{handle_type::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        std::suspend_always yield_value(T v) noexcept { current = std::move(v); return {}; }
        void return_void()         noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;

    bool next() { handle_.resume(); return !handle_.done(); }
    T const& value() const { return handle_.promise().current; }

    explicit Generator(handle_type h) noexcept : handle_(h) {}
    Generator(Generator&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Generator() { if (handle_) handle_.destroy(); }
    Generator(const Generator&) = delete;
private:
    handle_type handle_;
};

// ── Minimal Task<T> ───────────────────────────────────────────────────
template<typename T>
class Task
{
public:
    struct promise_type
    {
        T result{};
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept
        { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct FA {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept
            { auto c = h.promise().continuation; return c ? c : std::noop_coroutine(); }
            void await_resume() noexcept {}
        };
        FA final_suspend() noexcept { return {}; }
        void return_value(T v) { result = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() noexcept { return false; }
    handle_type await_suspend(std::coroutine_handle<> c) noexcept
    { handle_.promise().continuation = c; return handle_; }
    T await_resume()
    {
        if (handle_.promise().exception)
            std::rethrow_exception(handle_.promise().exception);
        return std::move(handle_.promise().result);
    }
    T sync_wait() { handle_.resume(); return await_resume(); }

    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Task() { if (handle_) handle_.destroy(); }
    Task(const Task&) = delete;
private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// 1. Cancellable generator — polls stop_token between yields
// ─────────────────────────────────────────────────────────────────────
Generator<int> counting_gen(std::stop_token token)
{
    for (int i = 0; ; ++i) {
        if (token.stop_requested()) {
            std::cout << "[gen] cancellation detected at i=" << i << "\n";
            co_return; // clean exit
        }
        co_yield i;
    }
}

// ─────────────────────────────────────────────────────────────────────
// 2. CancellableDelay — awaitable that can be interrupted
//
// Registers a stop_callback that resumes the coroutine early when
// cancellation is requested, setting a `cancelled` flag.
// ─────────────────────────────────────────────────────────────────────
struct CancellableDelay
{
    std::chrono::milliseconds duration;
    std::stop_token           token;
    bool                      cancelled = false;

    bool await_ready() noexcept
    {
        return token.stop_requested(); // skip if already cancelled
    }

    void await_suspend(std::coroutine_handle<> h)
    {
        // Register a callback: if stop requested, resume immediately
        auto* done = new std::atomic<bool>(false);

        std::thread([h, dur = duration, done]() mutable {
            std::this_thread::sleep_for(dur);
            if (!done->exchange(true))
                h.resume();
            delete done;
        }).detach();

        // stop_callback fires when stop is requested
        // We store it as a member to keep it alive; here inline via thread
        // In production use std::stop_callback member field
        (void)std::stop_callback(token, [h, done]() mutable {
            if (!done->exchange(true))
                h.resume();
        });
    }

    void await_resume() noexcept
    {
        cancelled = token.stop_requested();
    }
};

// ─────────────────────────────────────────────────────────────────────
// 3. Cancellable task: uses CancellableDelay at each await point
// ─────────────────────────────────────────────────────────────────────
Task<std::string> long_operation(std::stop_token token)
{
    std::cout << "[task] step 1\n";
    {
        CancellableDelay d{std::chrono::milliseconds(30), token};
        co_await d;
        if (d.cancelled) { std::cout << "[task] cancelled at step 1\n"; co_return "cancelled"; }
    }

    std::cout << "[task] step 2\n";
    {
        CancellableDelay d{std::chrono::milliseconds(30), token};
        co_await d;
        if (d.cancelled) { std::cout << "[task] cancelled at step 2\n"; co_return "cancelled"; }
    }

    std::cout << "[task] step 3 (done)\n";
    co_return "completed";
}

// ─────────────────────────────────────────────────────────────────────
// 4. jthread integration: stop_token comes from the thread itself
// ─────────────────────────────────────────────────────────────────────
void jthread_demo()
{
    std::cout << "\n=== jthread stop_token ===\n";

    std::jthread worker([](std::stop_token token) {
        // Each iteration: check token and do work
        for (int i = 0; i < 20; ++i) {
            if (token.stop_requested()) {
                std::cout << "[jthread] stopping at i=" << i << "\n";
                return;
            }
            std::cout << "[jthread] working " << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    worker.request_stop(); // cooperative cancellation
    // jthread joins automatically on destruction
}

int main()
{
    // ── Cancellable generator ─────────────────────────────────────────
    std::cout << "=== cancellable generator ===\n";
    {
        std::stop_source src;
        auto gen = counting_gen(src.get_token());

        for (int i = 0; i < 5; ++i)
            if (gen.next())
                std::cout << "  " << gen.value() << "\n";

        src.request_stop(); // signal cancellation

        // Next pull will see the token and co_return cleanly
        gen.next();
    }

    // ── Cancellable task: completes before cancel ─────────────────────
    std::cout << "\n=== task completes normally ===\n";
    {
        std::stop_source src;
        auto result = long_operation(src.get_token()).sync_wait();
        std::cout << "[main] result: " << result << "\n";
    }

    // ── Cancellable task: cancelled mid-operation ─────────────────────
    std::cout << "\n=== task cancelled mid-operation ===\n";
    {
        std::stop_source src;

        // Cancel after 15ms (between step 1 and step 2)
        std::thread([&src] {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            std::cout << "[main] requesting stop\n";
            src.request_stop();
        }).detach();

        auto result = long_operation(src.get_token()).sync_wait();
        std::cout << "[main] result: " << result << "\n";
    }

    jthread_demo();
}
