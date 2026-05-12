#include <iostream>
#include <thread>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────
// std::thread: create a thread by passing a callable + arguments.
// You must call join() or detach() before the thread object is destroyed,
// otherwise std::terminate is called.
//
// std::jthread (C++20): same as thread but joins automatically in its
// destructor and supports cooperative cancellation via std::stop_token.
// ─────────────────────────────────────────────────────────────────────

void worker(int id)
{
    std::cout << "thread " << id << " running\n";
}

int main()
{
    // ── Basic thread ──────────────────────────────────────────────────
    std::thread t1(worker, 1);
    t1.join(); // block until t1 finishes

    // ── Lambda thread ─────────────────────────────────────────────────
    std::thread t2([]{ std::cout << "lambda thread\n"; });
    t2.join();

    // ── Thread id and hardware concurrency ────────────────────────────
    std::cout << "main thread id: " << std::this_thread::get_id() << "\n";
    std::cout << "hw threads: "     << std::thread::hardware_concurrency() << "\n";

    // ── Detach ────────────────────────────────────────────────────────
    // The OS thread runs independently; the std::thread object can be
    // destroyed without joining. After detach you cannot join.
    // Only safe when the thread does not access locals that may be destroyed.
    std::thread t3([]{ /* no local captures — safe to detach */ });
    t3.detach();

    // ── jthread: auto-join + stop token (C++20) ───────────────────────
    // The destructor calls request_stop() then join() automatically.
    // The thread checks stop_requested() to cooperate with cancellation.
    {
        std::jthread jt([](std::stop_token st){
            int tick = 0;
            while (!st.stop_requested()) {
                std::cout << "jthread tick " << ++tick << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout << "jthread stopping\n";
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        jt.request_stop(); // signal cooperative stop
    } // jt destructor joins here — output: ~3 ticks then "jthread stopping"
}
