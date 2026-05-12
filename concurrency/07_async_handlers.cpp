#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────
// Async handler pattern: a single background thread draining a task queue.
//
// Tasks are std::function<void()> — any callable, including lambdas with
// captures. The caller "posts" a task and returns immediately; the worker
// thread executes it later.
//
// Key rule: tasks execute OUTSIDE the lock so the poster is never blocked
// by how long the task takes.
// ─────────────────────────────────────────────────────────────────────

class TaskQueue
{
public:
    TaskQueue() : worker_([this]{ run(); }) {}

    ~TaskQueue()
    {
        {
            std::lock_guard lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }

    // Post a task for async execution. Returns immediately.
    void post(std::function<void()> task)
    {
        {
            std::lock_guard lk(mtx_);
            queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void run()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lk(mtx_);
                cv_.wait(lk, [this]{ return !queue_.empty() || stop_; });
                if (queue_.empty()) return; // stop_ set and nothing left
                task = std::move(queue_.front());
                queue_.pop();
            }
            task(); // execute outside the lock
        }
    }

    std::queue<std::function<void()>> queue_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    bool                              stop_{false};
    std::thread                       worker_;
};

int main()
{
    TaskQueue tq;

    for (int i = 0; i < 5; ++i) {
        tq.post([i]{
            std::cout << "handler " << i
                      << " on thread " << std::this_thread::get_id() << "\n";
        });
    }

    // Give tasks time to drain before the destructor stops the worker.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "main on thread " << std::this_thread::get_id() << "\n";
    // All handler thread ids match the single worker — different from main.
}
