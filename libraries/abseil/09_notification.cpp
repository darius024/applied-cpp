#include <absl/synchronization/notification.h>
#include <absl/synchronization/barrier.h>
#include <absl/synchronization/blocking_counter.h>
#include <absl/time/time.h>
#include <iostream>
#include <thread>
#include <vector>

// compile: g++ -std=c++17 09_notification.cpp -o 09_notification \
//          -labsl_synchronization -labsl_base -labsl_time \
//          -labsl_strings -pthread

// ─────────────────────────────────────────────────────────────────────
// High-level synchronisation primitives — built on absl::Mutex/CondVar.
//
// absl::Notification  — one-shot "ready" flag.
//   Notify()                 — set the flag; wake all waiters.
//   WaitForNotification()    — block until notified.
//   WaitForNotificationWithTimeout(d) — with deadline.
//   HasBeenNotified()        — non-blocking poll.
//   Use case: signal that initialisation is complete, a worker is done,
//   or a resource is available.
//
// absl::Barrier(N)  — rendezvous: all N threads must call Block()
//   before any of them continues. Returns true for the "last arrival"
//   thread, useful when one thread needs to do post-barrier work.
//   Use case: synchronise phases in a parallel algorithm.
//
// absl::BlockingCounter(N)  — countdown latch.
//   DecrementCount()  — called N times (typically by worker threads).
//   Wait()            — blocks until count reaches zero.
//   Use case: fan-out work to N tasks and wait for all to finish.
// ─────────────────────────────────────────────────────────────────────

// ── Notification: worker signals main ────────────────────────────────
void demo_notification()
{
    absl::Notification ready;

    std::thread worker([&ready] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::cout << "[worker] initialisation done\n";
        ready.Notify();
    });

    ready.WaitForNotification();
    std::cout << "[main] worker is ready\n";
    worker.join();

    // Non-blocking poll after notification
    std::cout << "HasBeenNotified: " << std::boolalpha
              << ready.HasBeenNotified() << "\n"; // true
}

// ── Barrier: phase synchronisation ───────────────────────────────────
void demo_barrier()
{
    const int N = 4;
    absl::Barrier barrier(N);

    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i)
        threads.emplace_back([i, &barrier] {
            std::cout << "[thread " << i << "] phase 1 done\n";
            bool last = barrier.Block(); // wait for all threads
            if (last) std::cout << "[last arrival] proceeding to phase 2\n";
            std::cout << "[thread " << i << "] phase 2\n";
        });

    for (auto& t : threads) t.join();
}

// ── BlockingCounter: fan-out / join ───────────────────────────────────
void demo_blocking_counter()
{
    const int TASKS = 6;
    absl::BlockingCounter latch(TASKS);

    std::vector<std::thread> workers;
    for (int i = 0; i < TASKS; ++i)
        workers.emplace_back([i, &latch] {
            std::cout << "[task " << i << "] complete\n";
            latch.DecrementCount();
        });

    latch.Wait(); // blocks until all 6 tasks call DecrementCount()
    std::cout << "[main] all tasks finished\n";

    for (auto& w : workers) w.join();
}

int main()
{
    std::cout << "── Notification ──\n";
    demo_notification();

    std::cout << "\n── Barrier ──\n";
    demo_barrier();

    std::cout << "\n── BlockingCounter ──\n";
    demo_blocking_counter();
}
