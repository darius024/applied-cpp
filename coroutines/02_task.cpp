// compile: g++ -std=c++20 -O2 -o 02_task 02_task.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// ─────────────────────────────────────────────────────────────────────
// Task<T> — lazy async operation that produces a single value.
//
// Design decisions:
//   - Lazy: body does NOT start until the task is co_await-ed.
//     (initial_suspend returns suspend_always)
//   - Symmetric transfer: when the task completes, final_suspend
//     tail-calls back to the continuation coroutine — no stack growth
//     regardless of chain depth.
//   - Exceptions: stored in the promise and rethrown by await_resume,
//     so co_await propagates exceptions naturally.
//   - Ownership: Task<T> uniquely owns its frame; non-copyable, movable.
//
// sync_wait(): a helper to drive a top-level task from synchronous code
// (e.g. main). Only suitable when there is no real async I/O; real
// applications use an I/O loop (Asio, io_uring) that calls resume().
// ─────────────────────────────────────────────────────────────────────

template<typename T>
class Task
{
public:
    // ── promise_type ─────────────────────────────────────────────────
    struct promise_type
    {
        // Holds nothing / result / exception
        std::variant<std::monostate, T, std::exception_ptr> result;
        std::coroutine_handle<> continuation; // who is awaiting us

        Task get_return_object() noexcept
        {
            return Task{handle_type::from_promise(*this)};
        }

        // Lazy start
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Symmetric transfer to continuation on completion
        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> self) noexcept
            {
                auto cont = self.promise().continuation;
                // If no continuation (top-level), transfer to noop
                return cont ? cont : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T v)
        {
            result.template emplace<1>(std::move(v));
        }

        void unhandled_exception()
        {
            result.template emplace<2>(std::current_exception());
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    // ── Task is awaitable: co_await task ─────────────────────────────
    bool await_ready() noexcept { return false; }

    // Set current coroutine as continuation, then symmetric-transfer
    // into this task (starts it immediately)
    handle_type await_suspend(std::coroutine_handle<> cont) noexcept
    {
        handle_.promise().continuation = cont;
        return handle_; // tail-call into this task
    }

    T await_resume()
    {
        auto& r = handle_.promise().result;
        if (std::holds_alternative<std::exception_ptr>(r))
            std::rethrow_exception(std::get<std::exception_ptr>(r));
        return std::get<T>(std::move(r));
    }

    // ── sync_wait: run from synchronous code ─────────────────────────
    T sync_wait()
    {
        handle_.resume(); // kick off the lazy task
        return await_resume();
    }

    // ── Lifecycle ─────────────────────────────────────────────────────
    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Task() { if (handle_) handle_.destroy(); }

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// Task<void> specialisation
// ─────────────────────────────────────────────────────────────────────
template<>
class Task<void>
{
public:
    struct promise_type
    {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept
        {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> self) noexcept
            {
                auto cont = self.promise().continuation;
                return cont ? cont : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() noexcept { return false; }

    handle_type await_suspend(std::coroutine_handle<> cont) noexcept
    {
        handle_.promise().continuation = cont;
        return handle_;
    }

    void await_resume()
    {
        if (handle_.promise().exception)
            std::rethrow_exception(handle_.promise().exception);
    }

    void sync_wait()
    {
        handle_.resume();
        await_resume();
    }

    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Task() { if (handle_) handle_.destroy(); }

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// Examples
// ─────────────────────────────────────────────────────────────────────

Task<int> compute(int x)
{
    co_return x * x;
}

// Chains: awaits compute(), transforms result
Task<std::string> format_result(int x)
{
    int sq = co_await compute(x);
    co_return std::to_string(x) + "^2 = " + std::to_string(sq);
}

// Deep chain: three levels of co_await
Task<int> level_c() { co_return 7; }
Task<int> level_b() { co_return co_await level_c() + 1; }
Task<int> level_a() { co_return co_await level_b() * 2; }

// Exception propagation through co_await chain
Task<int> will_throw()
{
    throw std::runtime_error("something went wrong");
    co_return 0;
}

Task<int> catches_exception()
{
    try {
        co_return co_await will_throw();
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
        co_return -1;
    }
}

int main()
{
    // Single level
    std::cout << compute(5).sync_wait() << "\n"; // 25

    // Chained
    std::cout << format_result(6).sync_wait() << "\n"; // 6^2 = 36

    // Deep chain — symmetric transfer prevents stack growth
    std::cout << "level_a: " << level_a().sync_wait() << "\n"; // 16

    // Exception propagation
    std::cout << "exception result: " << catches_exception().sync_wait() << "\n"; // -1
}
