// compile: g++ -std=c++20 -O2 -o 06_scheduler 06_scheduler.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <iostream>
#include <queue>
#include <string>
#include <utility>

// ─────────────────────────────────────────────────────────────────────
// Cooperative round-robin scheduler.
//
// A scheduler owns a queue of ready coroutine handles. It runs them
// one at a time, each running until it voluntarily yields control.
//
//   co_await yield()  — suspend this coroutine; re-enqueue it; run next
//   scheduler.spawn() — add a new coroutine to the run queue
//   scheduler.run()   — drain the queue until all tasks complete
//
// This is the heart of single-threaded async runtimes (Node.js, Python
// asyncio, early versions of many C++ frameworks). No OS threads needed.
//
// Key insight: coroutines = cheap "green threads" cooperating via
// explicit yield points. Stack is on the heap (frame), so thousands
// can coexist with negligible memory overhead.
//
// Note: this scheduler is single-threaded and not thread-safe.
// ─────────────────────────────────────────────────────────────────────

class Scheduler
{
public:
    // Schedule an already-created coroutine handle for execution
    void enqueue(std::coroutine_handle<> h)
    {
        ready_.push(h);
    }

    // Drive until all tasks complete
    void run()
    {
        while (!ready_.empty()) {
            auto h = ready_.front();
            ready_.pop();
            h.resume(); // run until next suspension or completion
            // If not done, the coroutine re-enqueued itself via yield()
        }
    }

    // Returns an awaitable: suspend current task, re-enqueue it, run others
    auto yield()
    {
        struct YieldAwaiter
        {
            Scheduler& sched;

            bool await_ready() noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h) noexcept
            {
                sched.enqueue(h); // put self back at end of queue
            }

            void await_resume() noexcept {}
        };
        return YieldAwaiter{*this};
    }

    // Spawn a coroutine defined by a callable; enqueue it
    template<typename F>
    void spawn(F&& fn)
    {
        // F() must return a coroutine whose handle we can manage.
        // We use a simple Job type for this.
        auto job = std::forward<F>(fn)();
        enqueue(job.handle_);
        job.handle_ = nullptr; // scheduler takes ownership
    }

    // Job type: the return type of tasks submitted to the scheduler.
    // Suspends immediately; scheduler drives it.
    struct Job
    {
        struct promise_type
        {
            Job get_return_object() noexcept
            {
                return Job{std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend()   noexcept { return {}; }
            void return_void()      noexcept {}
            void unhandled_exception() noexcept {}
        };

        std::coroutine_handle<promise_type> handle_;

        explicit Job(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
        Job(Job&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
        ~Job() { if (handle_) handle_.destroy(); }
        Job(const Job&) = delete;
    };

private:
    std::queue<std::coroutine_handle<>> ready_;
};

// Global scheduler instance shared by all tasks in this example
Scheduler sched;

// ─────────────────────────────────────────────────────────────────────
// Example tasks
// ─────────────────────────────────────────────────────────────────────

// Counter task: counts to n, yielding after each step
Scheduler::Job counter(std::string name, int n)
{
    for (int i = 1; i <= n; ++i) {
        std::cout << "[" << name << "] step " << i << "/" << n << "\n";
        co_await sched.yield(); // give other tasks a turn
    }
    std::cout << "[" << name << "] done\n";
}

// Producer/consumer via a shared queue — classic cooperative pattern
std::queue<int> shared_queue;

Scheduler::Job producer(int count)
{
    for (int i = 0; i < count; ++i) {
        shared_queue.push(i);
        std::cout << "[producer] pushed " << i << "\n";
        co_await sched.yield();
    }
}

Scheduler::Job consumer(int count)
{
    int received = 0;
    while (received < count) {
        if (!shared_queue.empty()) {
            int v = shared_queue.front();
            shared_queue.pop();
            std::cout << "[consumer] got " << v << "\n";
            ++received;
        }
        co_await sched.yield();
    }
}

// Nested spawn: a task that dynamically spawns sub-tasks
Scheduler::Job spawner()
{
    std::cout << "[spawner] spawning sub-tasks\n";
    sched.spawn([] { return counter("sub-A", 2); });
    sched.spawn([] { return counter("sub-B", 2); });
    co_await sched.yield(); // let them run
    std::cout << "[spawner] done\n";
}

int main()
{
    std::cout << "=== round-robin interleaving ===\n";
    sched.spawn([] { return counter("alpha", 3); });
    sched.spawn([] { return counter("beta",  3); });
    sched.spawn([] { return counter("gamma", 2); });
    sched.run();

    std::cout << "\n=== producer / consumer ===\n";
    sched.spawn([] { return producer(4); });
    sched.spawn([] { return consumer(4); });
    sched.run();

    std::cout << "\n=== dynamic spawn ===\n";
    sched.spawn([] { return spawner(); });
    sched.run();
}
