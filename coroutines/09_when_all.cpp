// compile: g++ -std=c++20 -O2 -pthread -o 09_when_all 09_when_all.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

// ─────────────────────────────────────────────────────────────────────
// Structured concurrency: when_all — start N tasks in parallel,
// resume the parent only after all have completed.
//
// Pattern:
//   - Create a shared counter initialised to N.
//   - Each child task, on completion, atomically decrements the counter.
//   - The last one to finish (counter hits 0) resumes the parent.
//   - Parent is suspended via a custom awaitable that stores its handle.
//
// This avoids any dynamic allocation of join logic and composes
// naturally with the Task coroutine machinery.
//
// Two variants shown:
//   when_all_void(tasks...) — Task<void>, results discarded
//   when_all_values(tasks...) — Task<T>, collects results into tuple
//
// Thread pool is reused from 05_thread_hop.cpp (inline here).
// ─────────────────────────────────────────────────────────────────────

// ── Thread pool (minimal) ─────────────────────────────────────────────
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
        { std::unique_lock lk(mu_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    void post(std::function<void()> fn)
    {
        { std::unique_lock lk(mu_); q_.push(std::move(fn)); }
        cv_.notify_one();
    }
    auto schedule()
    {
        struct A {
            ThreadPool& p;
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h)
            { p.post([h]() mutable { h.resume(); }); }
            void await_resume() noexcept {}
        };
        return A{*this};
    }
private:
    void worker()
    {
        while (true) {
            std::function<void()> fn;
            { std::unique_lock lk(mu_);
              cv_.wait(lk, [this]{ return stop_ || !q_.empty(); });
              if (stop_ && q_.empty()) return;
              fn = std::move(q_.front()); q_.pop(); }
            fn();
        }
    }
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
};

ThreadPool pool(4);

// ── Task<T> (identical to 02_task.cpp) ───────────────────────────────
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
// WhenAllContext: shared state for N concurrently running tasks
// ─────────────────────────────────────────────────────────────────────
struct WhenAllContext
{
    std::atomic<int>        remaining;
    std::coroutine_handle<> parent;

    explicit WhenAllContext(int n) : remaining(n) {}

    // Called by each child on completion; last one resumes parent
    void notify()
    {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            parent.resume();
    }
};

// ── Awaitable that suspends until the counter reaches zero ────────────
struct WhenAllAwaiter
{
    WhenAllContext& ctx;
    bool await_ready() noexcept { return ctx.remaining.load() == 0; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ctx.parent = h; }
    void await_resume() noexcept {}
};

// ── Wrap a Task<T> to notify the context when done ───────────────────
template<typename T>
Task<void> notify_on_done(Task<T> task, WhenAllContext& ctx, T& out)
{
    out = co_await std::move(task);
    ctx.notify();
}

// ─────────────────────────────────────────────────────────────────────
// when_all: start tasks on the thread pool; wait for all
// ─────────────────────────────────────────────────────────────────────
template<typename... Ts>
Task<std::tuple<Ts...>> when_all(Task<Ts>... tasks)
{
    std::tuple<Ts...> results;
    WhenAllContext ctx(sizeof...(Ts));

    // Launch each task on the pool via a wrapper that notifies on done
    auto launch = [&]<std::size_t... I>(std::index_sequence<I...>) {
        // Fold: start each wrapper task
        (pool.post([
            wrapper = notify_on_done(
                std::move(tasks),
                ctx,
                std::get<I>(results))]() mutable
        {
            // wrapper is a lazy Task<void>; kick it off
            const_cast<Task<void>&>(wrapper).sync_wait();
        }), ...);
    };
    launch(std::index_sequence_for<Ts...>{});

    // Suspend until all tasks notify
    co_await WhenAllAwaiter{ctx};

    co_return results;
}

// ─────────────────────────────────────────────────────────────────────
// Example tasks
// ─────────────────────────────────────────────────────────────────────

Task<int> fetch_int(int v, int delay_ms)
{
    co_await pool.schedule();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    std::cout << "  task(" << v << ") done on thread "
              << std::this_thread::get_id() << "\n";
    co_return v;
}

Task<std::string> fetch_str(std::string s, int delay_ms)
{
    co_await pool.schedule();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    std::cout << "  task(\"" << s << "\") done\n";
    co_return s;
}

int main()
{
    std::cout << "launching 3 tasks concurrently\n";

    auto t = when_all(
        fetch_int(10, 60),
        fetch_int(20, 20),
        fetch_str("hello", 40)
    );

    auto [a, b, c] = t.sync_wait();

    std::cout << "results: " << a << ", " << b << ", " << c << "\n";
}
