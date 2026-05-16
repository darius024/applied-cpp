// compile: g++ -std=c++20 -O2 -pthread -o 05_thread_hop 05_thread_hop.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <iostream>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <utility>

// ─────────────────────────────────────────────────────────────────────
// Thread-hopping: switch which thread a coroutine runs on at any
// co_await point.
//
// This is one of the most important coroutine patterns for high-
// performance systems. It allows:
//   - Moving CPU-bound work onto a thread pool without callbacks.
//   - Returning to an I/O thread after compute work completes.
//   - Expressing "run this piece on that pool" inline with the logic.
//
// How it works:
//   co_await pool.schedule()
//     → await_ready() returns false
//     → await_suspend() posts the coroutine handle to the pool's queue
//     → the current thread returns (unblocked)
//     → a pool thread dequeues the handle and calls resume()
//     → execution continues on the pool thread from the co_await point
//
// The coroutine's stack variables and CPU registers are preserved in
// the heap-allocated frame — perfectly safe across thread boundaries.
// ─────────────────────────────────────────────────────────────────────

// ── Thread pool ───────────────────────────────────────────────────────
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            threads_.emplace_back([this] { worker(); });
    }

    ~ThreadPool()
    {
        {
            std::unique_lock lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }

    // Post any callable
    void post(std::function<void()> fn)
    {
        {
            std::unique_lock lock(mu_);
            queue_.push(std::move(fn));
        }
        cv_.notify_one();
    }

    // Returns an awaitable that, when co_await-ed, resumes the
    // coroutine on this pool.
    auto schedule()
    {
        struct Awaiter
        {
            ThreadPool& pool;

            bool await_ready() noexcept { return false; } // always hop

            void await_suspend(std::coroutine_handle<> h)
            {
                pool.post([h]() mutable { h.resume(); });
            }

            void await_resume() noexcept {}
        };
        return Awaiter{*this};
    }

private:
    void worker()
    {
        while (true) {
            std::function<void()> fn;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                fn = std::move(queue_.front());
                queue_.pop();
            }
            fn();
        }
    }

    std::vector<std::thread>       threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex                     mu_;
    std::condition_variable        cv_;
    bool                           stop_ = false;
};

// ── Minimal Task<T> (identical to 02_task.cpp) ────────────────────────
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
        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() noexcept { return false; }
    handle_type await_suspend(std::coroutine_handle<> c) noexcept
    {
        handle_.promise().continuation = c;
        return handle_;
    }
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
// Examples
// ─────────────────────────────────────────────────────────────────────

ThreadPool io_pool(2);      // simulates I/O threads
ThreadPool compute_pool(4); // simulates CPU worker threads

auto tid() { return std::this_thread::get_id(); }

// Coroutine that hops between pools mid-execution
Task<int> process_request(int id)
{
    std::cout << "[req " << id << "] start on thread " << tid() << "\n";

    // Hop to compute pool for CPU work
    co_await compute_pool.schedule();
    std::cout << "[req " << id << "] compute on thread " << tid() << "\n";
    int result = id * id; // simulate CPU work

    // Hop back to I/O pool to send the response
    co_await io_pool.schedule();
    std::cout << "[req " << id << "] send on thread " << tid() << "\n";

    co_return result;
}

// Multiple hops: demonstrate that locals survive across thread boundaries
Task<std::string> multi_hop()
{
    int   x = 10;          // local variable on the coroutine frame
    float y = 3.14f;

    co_await compute_pool.schedule();
    x *= 2;                // x is 20, still accessible on a different thread

    co_await io_pool.schedule();
    y += 1.0f;             // y is 4.14f, on yet another thread

    co_return "x=" + std::to_string(x) + " y=" + std::to_string(y);
}

int main()
{
    std::cout << "main thread: " << tid() << "\n\n";

    // Fire several requests; each hops threads independently
    // Use a latch-like atomic to know when all are done
    std::atomic<int> done{0};
    const int N = 4;

    // We need an outer coroutine to co_await; drive via detached threads
    std::vector<std::thread> launchers;
    for (int i = 0; i < N; ++i) {
        launchers.emplace_back([i, &done] {
            auto t = process_request(i);
            int r  = t.sync_wait();
            std::cout << "[req " << i << "] result=" << r << "\n";
            ++done;
        });
    }
    for (auto& t : launchers) t.join();

    // Multi-hop with locals surviving thread boundaries
    std::cout << "\nmulti_hop: " << multi_hop().sync_wait() << "\n";
}
