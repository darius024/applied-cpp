#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <numeric>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#  include <x86intrin.h>  // __rdtsc(), __rdtscp()
#  define HAVE_RDTSC 1
#elif defined(__aarch64__)
#  define HAVE_RDTSC 0
#else
#  define HAVE_RDTSC 0
#endif

// ─────────────────────────────────────────────────────────────────────
// Latency measurement: clock_gettime, std::chrono, RAII timer, rdtsc,
// latency histogram, and percentile analysis.
//
// Choosing a clock:
//   std::chrono::steady_clock     — POSIX monotonic; ~20–30 ns overhead;
//                                    the right default for benchmarks.
//   clock_gettime(CLOCK_MONOTONIC) — same underlying source; C API.
//   clock_gettime(CLOCK_REALTIME)  — wall clock; subject to NTP jumps;
//                                    never use for latency measurement.
//   __rdtsc() (x86)                — CPU cycle counter; <5 ns overhead;
//                                    best for ultra-fine measurement.
//                                    Caveat: TSC may not be synchronised
//                                    across NUMA nodes; use rdtscp +
//                                    CPUID fence for strict ordering.
//   mach_absolute_time() (macOS)   — same role as rdtsc on Apple Silicon.
//
// Measurement pitfalls:
//   • Cold cache — first call brings code/data into cache; skip it.
//   • Compiler dead-code elimination — if the result is unused the
//     compiler removes the timed work; use a volatile sink or asm clobber.
//   • Frequency scaling — CPU may boost on first heavy use; warm up.
//   • Timer overhead — subtract a no-op loop to isolate work cost.
//
// Percentile analysis:
//   p50 = typical; p95/p99/p99.9 = tail latency.
//   Collect N samples, sort, index.  In HFT: p99.9 often matters more
//   than p50 — a 1-in-1000 slow call can cause a missed opportunity.
//
// RAII timer:
//   Scope-based; stores start time on construction, prints on destruction.
//   Zero-overhead in release builds when used with a no-op sink.
//
// Compile:  g++ -std=c++20 -O2 06_latency_timing.cpp -o timing && ./timing
// ─────────────────────────────────────────────────────────────────────

// ── clock_gettime wrapper ─────────────────────────────────────────────
static std::int64_t now_ns() noexcept
{
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL
         + ts.tv_nsec;
}

// ── RAII scoped timer ─────────────────────────────────────────────────
struct ScopedTimer {
    const char*    label;
    std::int64_t   start;

    explicit ScopedTimer(const char* l) noexcept
        : label{l}, start{now_ns()} {}

    ~ScopedTimer() noexcept {
        std::int64_t elapsed = now_ns() - start;
        std::printf("  [%s] %.3f µs\n", label, elapsed / 1e3);
    }
};

// ── PART 1: RAII timer demo ───────────────────────────────────────────
static void demo_raii_timer()
{
    std::puts("── RAII scoped timer ────────────────────────────");
    {
        ScopedTimer t("vwap_calc");
        // Simulate some work.
        volatile double sum = 0.0;
        for (int i = 0; i < 1'000'000; ++i) sum += i * 0.001;
        (void)sum;
    }
}

// ── PART 2: std::chrono micro-benchmark ───────────────────────────────
// Warm-up pass eliminates cold-cache effects.
static void demo_chrono_bench()
{
    std::puts("\n── std::chrono benchmark (with warmup) ──────────");

    auto work = [](int n) {
        volatile double pnl = 0.0;
        for (int i = 0; i < n; ++i) pnl += i * 0.001;
        return pnl;
    };

    const int N    = 100'000;
    const int REPS = 200;

    // Warmup — runs the code once to populate I/D caches.
    work(N);

    std::vector<double> samples;
    samples.reserve(REPS);

    for (int r = 0; r < REPS; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        work(N);
        auto t1 = std::chrono::steady_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        samples.push_back(ns / static_cast<double>(N));
    }

    std::sort(samples.begin(), samples.end());
    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p / 100.0 * REPS);
        return samples[std::min(idx, samples.size() - 1)];
    };

    std::printf("  p50   = %.3f ns/iter\n", pct(50));
    std::printf("  p95   = %.3f ns/iter\n", pct(95));
    std::printf("  p99   = %.3f ns/iter\n", pct(99));
    std::printf("  max   = %.3f ns/iter\n", samples.back());
}

// ── PART 3: clock_gettime overhead measurement ────────────────────────
// Measure the cost of calling now_ns() itself — this is your measurement
// floor.  Any timed interval shorter than ~2× this is unreliable.
static void demo_clock_overhead()
{
    std::puts("\n── clock_gettime overhead ───────────────────────");

    const int N = 1000;
    std::vector<std::int64_t> times(N);

    for (int i = 0; i < N; ++i) {
        std::int64_t a = now_ns();
        std::int64_t b = now_ns();
        times[static_cast<std::size_t>(i)] = b - a;
    }

    std::sort(times.begin(), times.end());
    std::int64_t min_ns = times[0];
    std::int64_t p50_ns = times[N / 2];
    std::int64_t p99_ns = times[N * 99 / 100];
    std::printf("  clock overhead — min=%lld ns  p50=%lld ns  p99=%lld ns\n",
                (long long)min_ns, (long long)p50_ns, (long long)p99_ns);
    std::puts("  (minimum ≈ single clock_gettime cost)");
}

// ── PART 4: latency histogram ─────────────────────────────────────────
// Bucket samples into power-of-two ranges to give a distribution view
// without sorting N samples.  Used in production monitoring.
static void demo_histogram()
{
    std::puts("\n── latency histogram ────────────────────────────");

    const int N = 100'000;
    // Simulate latencies: mostly fast, occasional slow outliers.
    std::vector<std::int64_t> latencies;
    latencies.reserve(N);
    for (int i = 0; i < N; ++i) {
        std::int64_t a = now_ns();
        volatile double x = 0.0;
        // Variable-length work: occasionally heavier.
        int loops = (i % 1000 == 0) ? 5000 : 100;
        for (int j = 0; j < loops; ++j) x += j * 0.001;
        std::int64_t b = now_ns();
        latencies.push_back(b - a);
        (void)x;
    }

    // Bucket into: <100ns, 100-999ns, 1-9µs, 10-99µs, ≥100µs
    std::array<int, 5> buckets{};
    for (auto lat : latencies) {
        if      (lat <          100) ++buckets[0];
        else if (lat <        1'000) ++buckets[1];
        else if (lat <       10'000) ++buckets[2];
        else if (lat <      100'000) ++buckets[3];
        else                         ++buckets[4];
    }

    const char* labels[] = {"<100ns","100ns-1µs","1-10µs","10-100µs","≥100µs"};
    std::printf("  %-12s  %7s  %6s\n", "bucket", "count", "pct");
    for (int i = 0; i < 5; ++i)
        std::printf("  %-12s  %7d  %5.1f%%\n",
                    labels[i], buckets[i],
                    100.0 * buckets[i] / N);

    // Percentile from sorted latencies.
    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        return latencies[static_cast<std::size_t>(p / 100.0 * N)];
    };
    std::printf("\n  p50=%lld ns  p99=%lld ns  p99.9=%lld ns\n",
                (long long)pct(50), (long long)pct(99), (long long)pct(99.9));
}

// ── PART 5: rdtsc cycle counter (x86 only) ────────────────────────────
#if HAVE_RDTSC
static void demo_rdtsc()
{
    std::puts("\n── rdtsc cycle counter (x86) ────────────────────");
    // rdtscp + memory clobber to prevent instruction reordering.
    unsigned int aux;
    uint64_t t0 = __rdtscp(&aux);
    asm volatile("" ::: "memory"); // ordering fence
    volatile double sum = 0.0;
    for (int i = 0; i < 1000; ++i) sum += i;
    asm volatile("" ::: "memory");
    uint64_t t1 = __rdtscp(&aux);
    std::printf("  1000 iterations: %llu cycles\n",
                (unsigned long long)(t1 - t0));
    (void)sum;
}
#else
static void demo_rdtsc()
{
    std::puts("\n── rdtsc: not available on this architecture ─────");
    std::puts("  On macOS ARM: use mach_absolute_time() from <mach/mach_time.h>");
    std::puts("  On x86: __rdtscp() from <x86intrin.h>");
}
#endif

int main()
{
    demo_raii_timer();
    demo_chrono_bench();
    demo_clock_overhead();
    demo_histogram();
    demo_rdtsc();
}
