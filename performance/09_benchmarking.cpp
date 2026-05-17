#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Benchmarking methodology: manual harness, DoNotOptimize, warmup,
// stable timing, and common pitfalls.
//
// Google Benchmark (https://github.com/google/benchmark) is the standard
// C++ microbenchmark library.  This file implements an equivalent
// lightweight harness that compiles with just:
//   g++ -std=c++20 -O3 09_benchmarking.cpp -o bench && ./bench
//
// If you have Google Benchmark installed:
//   g++ -std=c++20 -O3 09_benchmarking.cpp -o bench -lbenchmark && ./bench
//
// Key ideas:
//
//   DoNotOptimize(x) — prevents the compiler from eliminating the
//     computation as dead code.  Uses either a volatile write or an
//     asm clobber to force the result to "escape" to an unknown
//     destination without actually writing to memory.
//
//     volatile sink  — forces the write; may restrict some opts.
//     asm("" : "+r"(v) :: "memory")  — GCC/Clang: touches register and
//       memory model without a real instruction.
//
//   ClobberMemory() — asm("" ::: "memory"); tells the compiler that all
//     memory state may have changed; forces all pending loads/stores to
//     be committed at this point.  Use before/after work that touches
//     global state you do not want the compiler to hoist out.
//
//   Warmup — run the loop at least once (or a few iterations) before
//     recording.  Populates the I-cache and D-cache.
//
//   Stable timer — take min over many runs, not mean.  The minimum
//     is the closest estimate of the true cost; noise only inflates.
//
//   Pitfall: inlining the SUT into the benchmark.
//     If the benchmarked function is inlined and the compiler can prove
//     the output is constant, it will compute it at compile time and
//     the benchmark measures nothing.  Mark the SUT with [[gnu::noinline]]
//     or pass inputs through a sink to prevent this.
//
// Compile:  g++ -std=c++20 -O3 09_benchmarking.cpp -o bench && ./bench
// ─────────────────────────────────────────────────────────────────────

// ── DoNotOptimize helpers ─────────────────────────────────────────────
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T& val) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r,m"(val) :: "memory");
#else
    volatile T* sink = &val;
    (void)*sink;
#endif
}

[[gnu::always_inline]] inline void clobber_memory() noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
#endif
}

// ── Benchmark harness ─────────────────────────────────────────────────
// Runs 'fn' for REPS iterations, records per-iteration time in ns,
// sorts, and prints percentiles.
template <typename Fn>
static void run_bench(const char* name, int n_per_iter, int reps, Fn fn)
{
    // Warmup: 3 iterations to populate caches.
    for (int i = 0; i < 3; ++i) fn();

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));

    for (int r = 0; r < reps; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                .count());
        samples.push_back(ns / static_cast<double>(n_per_iter));
    }

    std::sort(samples.begin(), samples.end());
    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p / 100.0 * reps);
        return samples[std::min(idx, samples.size() - 1)];
    };

    std::printf("  %-30s  min=%6.3f  p50=%6.3f  p99=%6.3f ns\n",
                name, samples[0], pct(50), pct(99));
}

// ── Functions under test ──────────────────────────────────────────────

[[gnu::noinline]] // prevent full inlining into benchmark
static double vwap(const double* __restrict__ prices,
                   const double* __restrict__ qtys, int n)
{
    double num = 0.0, den = 0.0;
    for (int i = 0; i < n; ++i) { num += prices[i] * qtys[i]; den += qtys[i]; }
    return den > 0.0 ? num / den : 0.0;
}

[[gnu::noinline]]
static double compute_ewma(const double* data, int n, double alpha)
{
    double ewma = data[0];
    for (int i = 1; i < n; ++i)
        ewma = alpha * data[i] + (1.0 - alpha) * ewma;
    return ewma;
}

// Demonstrates incorrect benchmark: if this is not noinline AND the
// input is a compile-time constant, the compiler may fold it away.
[[gnu::noinline]]
static int sum_array(const int* data, int n)
{
    int s = 0;
    for (int i = 0; i < n; ++i) s += data[i];
    return s;
}

int main()
{
    std::puts("── benchmarking harness ─────────────────────────");

    const int N    = 1024;
    const int REPS = 500;

    std::vector<double> prices(N), qtys(N);
    std::vector<int>    idata(N);
    for (int i = 0; i < N; ++i) {
        prices[static_cast<std::size_t>(i)] = 100.0 + i * 0.01;
        qtys[static_cast<std::size_t>(i)]   = 100.0;
        idata[static_cast<std::size_t>(i)]  = i;
    }

    // ── VWAP ─────────────────────────────────────────────────────────
    run_bench("vwap(N=1024)", N, REPS, [&] {
        double r = vwap(prices.data(), qtys.data(), N);
        do_not_optimize(r); // prevent dead-code elimination
    });

    // ── EWMA ──────────────────────────────────────────────────────────
    run_bench("ewma(N=1024, a=0.1)", N, REPS, [&] {
        double r = compute_ewma(prices.data(), N, 0.1);
        do_not_optimize(r);
    });

    // ── integer sum ───────────────────────────────────────────────────
    run_bench("sum_array(N=1024)", N, REPS, [&] {
        int r = sum_array(idata.data(), N);
        do_not_optimize(r);
    });

    // ── std::sort for comparison ──────────────────────────────────────
    std::vector<double> to_sort(N);
    run_bench("std::sort(N=1024)", N, REPS, [&] {
        std::copy(prices.begin(), prices.end(), to_sort.begin());
        clobber_memory(); // ensure the copy is committed
        std::sort(to_sort.begin(), to_sort.end());
        do_not_optimize(to_sort[0]);
    });

    // ── ClobberMemory demo ────────────────────────────────────────────
    std::puts("\n── ClobberMemory: forces flush of pending stores ─");
    std::array<double, 8> arr{};
    for (auto& v : arr) v = 1.0;
    clobber_memory(); // compiler must commit arr writes here
    // Now all downstream reads of arr see the writes above.
    std::printf("  arr[3] = %.1f  (reads after ClobberMemory)\n", arr[3]);

    // ── Pitfall: timing the timer itself ─────────────────────────────
    std::puts("\n── timer overhead measurement ────────────────────");
    const int TN = 1000;
    std::vector<std::int64_t> overhead(TN);
    for (int i = 0; i < TN; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto t1 = std::chrono::steady_clock::now();
        overhead[static_cast<std::size_t>(i)] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    std::sort(overhead.begin(), overhead.end());
    std::printf("  steady_clock overhead — min=%lld ns  p50=%lld ns\n",
                (long long)overhead[0], (long long)overhead[TN / 2]);
    std::puts("  (subtract this from any measurement shorter than ~10x this)");

    std::puts("\n── If you have Google Benchmark installed ─────────");
    std::puts("   Compile with: g++ -std=c++20 -O3 <file>.cpp -lbenchmark -lpthread");
    std::puts("   See doc.md for the BENCHMARK() macro API.");
}
