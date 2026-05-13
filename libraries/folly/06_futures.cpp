#include <folly/futures/Future.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/InlineExecutor.h>
#include <iostream>
#include <stdexcept>

// compile: g++ -std=c++17 06_futures.cpp -o 06_futures \
//          -lfolly -lglog -lgflags -lfmt -ldouble-conversion \
//          -lboost_system -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::Future<T> / Promise<T>: composable async values.
//
// Unlike std::future, folly futures support non-blocking continuations:
//   .thenValue(f)   — chains f(T) when the future resolves; returns Future<U>.
//   .thenError(f)   — handles exceptions without propagating.
//   .via(executor)  — routes the continuation to a specific executor.
//                     Critical for pinning CPU-heavy work to CPU pools
//                     and I/O callbacks to I/O pools.
//
// SemiFuture<T>: a future not yet bound to an executor (safe to
//   construct on any thread). Call .via(exec) to make it a Future<T>.
//
// collectAll(f1, f2, …) — returns Future<tuple<Try<T>…>>; always
//   resolves (errors are in Try, not thrown).
// collectAny(vec)       — resolves with the first to complete.
//
// Promise<T>: the write end. Call setValue(v) or setException(e).
// ─────────────────────────────────────────────────────────────────────

int main()
{
    folly::CPUThreadPoolExecutor pool(2);

    // ── makeFuture: immediately resolved ─────────────────────────────
    auto f = folly::makeFuture(42)
        .thenValue([](int v) { return v * 2; })   // 84
        .thenValue([](int v) { return v + 1; });  // 85

    std::cout << "chain result: " << std::move(f).get() << "\n"; // 85

    // ── via: route continuation to thread pool ────────────────────────
    auto heavy = folly::makeSemiFuture(10)
        .via(&pool)                               // subsequent continuations run on pool
        .thenValue([](int v) {
            // runs on pool thread
            return v * v;                         // 100
        })
        .thenValue([](int v) {
            return std::to_string(v) + " computed";
        });

    std::cout << "via result: " << std::move(heavy).get() << "\n";

    // ── Promise / Future pair ─────────────────────────────────────────
    folly::Promise<std::string> promise;
    auto fut = promise.getSemiFuture().via(&pool);

    std::thread producer([p = std::move(promise)]() mutable {
        p.setValue("hello from producer");
    });

    std::cout << "promise result: " << std::move(fut).get() << "\n";
    producer.join();

    // ── thenError: handle exceptions without try/catch ────────────────
    auto safe = folly::makeFuture<int>(std::runtime_error("oops"))
        .thenError(folly::tag_t<std::runtime_error>{},
                   [](const std::runtime_error& e) {
                       std::cout << "caught: " << e.what() << "\n";
                       return -1;
                   })
        .thenValue([](int v) { return v + 100; });

    std::cout << "after error recovery: " << std::move(safe).get() << "\n"; // 99

    // ── collectAll: wait for all, collect errors in Try ───────────────
    std::vector<folly::Future<int>> futs;
    for (int i = 0; i < 4; ++i)
        futs.push_back(folly::via(&pool, [i] { return i * i; }));

    auto results = folly::collectAll(std::move(futs)).get();
    std::cout << "collectAll: ";
    for (auto& t : results)
        std::cout << t.value() << " "; // 0 1 4 9 (any order)
    std::cout << "\n";
}
