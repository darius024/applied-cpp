#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────
// condition_variable: block a thread until another thread signals it.
//
// Always use the predicate overload of wait():
//   cv.wait(lock, predicate)
// This guards against spurious wakeups — the OS can unblock a waiting
// thread for no reason. The predicate overload loops internally and
// re-checks the predicate after every wakeup.
//
// Pattern: producer/consumer queue.
//   - Producer pushes items and calls notify_one().
//   - Consumers wait until the queue is non-empty.
//   - A `done` flag lets consumers exit when the producer is finished.
// ─────────────────────────────────────────────────────────────────────

std::queue<int>          work_queue;
std::mutex               qmtx;
std::condition_variable  cv;
bool                     done = false;

void producer()
{
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        {
            std::lock_guard lk(qmtx);
            work_queue.push(i);
            std::cout << "produced " << i << "\n";
        }
        cv.notify_one(); // wake one waiting consumer
    }

    {
        std::lock_guard lk(qmtx);
        done = true;
    }
    cv.notify_all(); // wake all consumers so they can exit
}

void consumer(int id)
{
    while (true) {
        std::unique_lock lk(qmtx);

        // Wait until there is work OR the producer is done.
        // The lock is held when the predicate is checked and when we wake.
        cv.wait(lk, []{ return !work_queue.empty() || done; });

        if (!work_queue.empty()) {
            int val = work_queue.front();
            work_queue.pop();
            lk.unlock();                // release before doing the work
            std::cout << "consumer " << id << " got " << val << "\n";
        } else {
            break; // done is true and queue is empty — exit
        }
    }
}

int main()
{
    std::thread p(producer);
    std::thread c1(consumer, 1);
    std::thread c2(consumer, 2);
    p.join(); c1.join(); c2.join();
}
