#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>

using namespace std::chrono;

// ─────────────────────────────────────────────────────────────────────
// Timer patterns using a dedicated background thread.
//
// One-shot:  sleep until time_point, fire callback once.
// Repeating: loop with sleep_until(next), advance next by the interval.
//            sleep_until is preferred over sleep_for because it absorbs
//            the time spent in the callback — ticks stay evenly spaced.
//
// Cancellation: set an atomic<bool> flag. The thread checks it after
// each wakeup. For early cancellation (before the sleep expires) use a
// condition_variable timed wait instead of sleep_until.
// ─────────────────────────────────────────────────────────────────────

// ── One-shot timer ────────────────────────────────────────────────────
class OneShotTimer
{
public:
    OneShotTimer(milliseconds delay, std::function<void()> cb)
        : t_([delay, cb = std::move(cb)]{
            std::this_thread::sleep_for(delay);
            cb();
        })
    {}

    ~OneShotTimer() { if (t_.joinable()) t_.join(); }

private:
    std::thread t_;
};

// ── Repeating timer ───────────────────────────────────────────────────
class RepeatingTimer
{
public:
    RepeatingTimer(milliseconds interval, std::function<void()> cb)
        : cancelled_{false}
        , t_([interval, cb = std::move(cb), &cancelled = cancelled_]{
            auto next = steady_clock::now() + interval;
            while (!cancelled.load(std::memory_order_acquire)) {
                std::this_thread::sleep_until(next);
                if (!cancelled.load(std::memory_order_acquire)) cb();
                next += interval; // advance by interval, not wall clock
            }
        })
    {}

    void cancel() { cancelled_.store(true, std::memory_order_release); }

    ~RepeatingTimer()
    {
        cancel();
        if (t_.joinable()) t_.join();
    }

private:
    std::atomic<bool> cancelled_;
    std::thread       t_;
    // Note: cancelled_ is declared before t_ so it is initialised first.
    // The thread captures a reference to it and accesses it while running.
    // The destructor cancels then joins — the reference is never dangling.
};

int main()
{
    // ── One-shot ──────────────────────────────────────────────────────
    {
        OneShotTimer t(milliseconds(50), []{ std::cout << "one-shot fired\n"; });
    } // destructor blocks until the callback has fired

    // ── Repeating ─────────────────────────────────────────────────────
    {
        int ticks = 0;
        RepeatingTimer rt(milliseconds(20), [&ticks]{
            std::cout << "tick " << ++ticks << "\n";
        });
        std::this_thread::sleep_for(milliseconds(110));
        rt.cancel(); // stop before destructor to control the cutoff point
    } // ~5 ticks expected

    std::cout << "done\n";
}
