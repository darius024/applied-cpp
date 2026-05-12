#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <future>
#include <memory>

// ─────────────────────────────────────────────────────────────────────
// Thread pool: N workers share one task queue.
//
// submit(f) wraps f in a packaged_task, pushes it to the queue,
// and returns a future for the result. Callers can either .get() the
// future to wait for the result, or discard it to fire-and-forget.
//
// Shutdown: set done_ flag → notify_all → join all workers.
// Workers drain any remaining tasks before exiting.
// ─────────────────────────────────────────────────────────────────────

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            workers_.emplace_back([this]{ work(); });
    }

    ~ThreadPool()
    {
        {
            std::lock_guard lk(mtx_);
            done_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    // Submit callable f, get back a future for its return value.
    template<typename F>
    auto submit(F f) -> std::future<decltype(f())>
    {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard lk(mtx_);
            queue_.push([task]{ (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void work()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lk(mtx_);
                cv_.wait(lk, [this]{ return !queue_.empty() || done_; });
                if (done_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            task(); // execute outside the lock
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    bool                              done_{false};
};

int main()
{
    ThreadPool pool(4);

    // Submit 8 tasks to a 4-thread pool; collect futures.
    std::vector<std::future<int>> results;
    for (int i = 0; i < 8; ++i)
        results.push_back(pool.submit([i]{ return i * i; }));

    // .get() blocks until each task completes.
    for (auto& f : results)
        std::cout << f.get() << " ";
    std::cout << "\n"; // 0 1 4 9 16 25 36 49 (order of completion may vary)
}
