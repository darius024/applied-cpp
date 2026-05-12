#include <iostream>
#include <future>
#include <thread>
#include <chrono>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// std::async     — run a callable asynchronously, get back a future.
// std::future<T> — one-time read of a result; .get() blocks until ready.
// std::promise<T>— write end of a future; lets one thread hand a value
//                  to another thread manually.
// std::packaged_task<F> — wraps a callable; its future captures the
//                  return value when the task is invoked.
// ─────────────────────────────────────────────────────────────────────

int heavy(int n)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return n * n;
}

int main()
{
    // ── std::async ────────────────────────────────────────────────────
    // launch::async  — guaranteed new thread, starts immediately.
    // launch::deferred — lazy: runs on the calling thread at .get().
    auto fut = std::async(std::launch::async, heavy, 7);
    std::cout << "doing other work while heavy() runs...\n";
    std::cout << "result=" << fut.get() << "\n"; // blocks until ready → 49

    // ── Exception propagation ─────────────────────────────────────────
    // Exceptions thrown in the async task are stored in the future and
    // re-thrown when .get() is called.
    auto bad = std::async(std::launch::async, []{
        throw std::runtime_error("oops");
        return 0;
    });
    try {
        bad.get();
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    // ── std::promise / std::future ────────────────────────────────────
    // When you need more control than async provides: one thread sets the
    // value, another reads it via the associated future.
    std::promise<int> prom;
    std::future<int>  fval = prom.get_future();

    std::thread setter([&prom]{
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        prom.set_value(100); // unblocks fval.get()
    });
    std::cout << "promise result=" << fval.get() << "\n"; // 100
    setter.join();

    // ── std::packaged_task ────────────────────────────────────────────
    // Wraps a callable so it can be passed around (e.g. to a thread pool)
    // while still returning a future for the result.
    std::packaged_task<int(int)> task(heavy);
    std::future<int> task_fut = task.get_future();

    std::thread runner(std::move(task), 9); // invoke task with arg 9
    std::cout << "packaged_task result=" << task_fut.get() << "\n"; // 81
    runner.join();
}
