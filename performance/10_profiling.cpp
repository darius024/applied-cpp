#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#  include <sys/syscall.h>
#  include <unistd.h>
#  include <linux/perf_event.h>
#  include <sys/ioctl.h>
#  define HAVE_PERF_EVENT 1
#else
#  define HAVE_PERF_EVENT 0
#endif

// ─────────────────────────────────────────────────────────────────────
// Profiling: RAII instrumentation, rdtsc sampling, and perf_event_open.
//
// Three layers of profiling:
//
//   1. Self-instrumentation (this file):
//      Insert timers around hot functions and accumulate results in a
//      ring buffer or hash map.  Zero external dependency; always on.
//      Suitable for production logging of p99/p99.9 latencies.
//
//   2. Sampling profilers:
//      External tool interrupts the process at regular intervals and
//      records the instruction pointer → call-stack histogram.
//        macOS:  Instruments (Time Profiler template) — drag-and-drop
//                  xcrun xctrace record --template "Time Profiler" ...
//        Linux:  perf record -g ./app && perf report
//        Both:   valgrind --tool=callgrind (instruction counts, not time)
//
//   3. Hardware counters (perf_event_open — Linux only):
//      Read CPU performance counters from within the program.
//      Counts: cycles, instructions, cache-misses, branch-misses.
//      Resolution: per-function or per-loop.
//
// RAII instrumentation pattern:
//   - On construction: record start timestamp.
//   - On destruction: compute elapsed; update a per-name accumulator.
//   - At end of run: dump the table.
//
// rdtsc sampling:
//   Instead of a full profiler, call __rdtsc() (or clock_gettime) at
//   entry/exit of key functions, accumulate min/sum/count, print at end.
//
// Compile:  g++ -std=c++20 -O2 10_profiling.cpp -o profiling && ./profiling
// ─────────────────────────────────────────────────────────────────────

// ── Thread-safe accumulator for per-site latency stats ────────────────
struct LatencyStats {
    std::atomic<std::int64_t> count{0};
    std::atomic<std::int64_t> total_ns{0};
    std::atomic<std::int64_t> min_ns{INT64_MAX};
    std::atomic<std::int64_t> max_ns{0};

    void record(std::int64_t ns) noexcept
    {
        count.fetch_add(1, std::memory_order_relaxed);
        total_ns.fetch_add(ns, std::memory_order_relaxed);
        // min: CAS loop
        std::int64_t cur = min_ns.load(std::memory_order_relaxed);
        while (ns < cur &&
               !min_ns.compare_exchange_weak(cur, ns,
                   std::memory_order_relaxed, std::memory_order_relaxed))
            ;
        // max: CAS loop
        cur = max_ns.load(std::memory_order_relaxed);
        while (ns > cur &&
               !max_ns.compare_exchange_weak(cur, ns,
                   std::memory_order_relaxed, std::memory_order_relaxed))
            ;
    }

    void print(const char* name) const noexcept
    {
        std::int64_t n = count.load();
        if (n == 0) return;
        double avg = static_cast<double>(total_ns.load()) / n;
        std::printf("  %-28s  n=%6lld  avg=%7.1f ns  min=%lld ns  max=%lld ns\n",
                    name, (long long)n, avg,
                    (long long)min_ns.load(), (long long)max_ns.load());
    }
};

// Global registry (for demo; in production use a thread-local ring buffer).
static LatencyStats g_stats[8];
static const char*  g_stat_names[8] = {
    "order_validate", "vwap_calc", "book_update",
    "risk_check",     "encode_order", "send_order",
    "", ""
};

// ── RAII scoped probe ─────────────────────────────────────────────────
struct Probe {
    int          slot;
    std::int64_t start;

    explicit Probe(int s) noexcept
        : slot{s},
          start{std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()} {}

    ~Probe() noexcept
    {
        std::int64_t end =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        g_stats[static_cast<std::size_t>(slot)].record(end - start);
    }
};

#define PROBE(slot) Probe _probe{slot}

// ── Functions that the profiling wraps ───────────────────────────────
[[gnu::noinline]] static bool validate_order(double price, int qty)
{
    PROBE(0); // slot 0 = "order_validate"
    volatile bool ok = (price > 0 && qty > 0);
    return ok;
}

[[gnu::noinline]] static double calc_vwap(const double* px,
                                           const double* sz, int n)
{
    PROBE(1); // slot 1 = "vwap_calc"
    double num = 0, den = 0;
    for (int i = 0; i < n; ++i) { num += px[i] * sz[i]; den += sz[i]; }
    return den > 0 ? num / den : 0.0;
}

[[gnu::noinline]] static void update_book(double* book, double px, double sz)
{
    PROBE(2); // slot 2 = "book_update"
    volatile double v = px * sz;
    (void)v;
    book[0] = px;
}

// ── PART 1: RAII probe demonstration ─────────────────────────────────
static void demo_raii_probe()
{
    std::puts("── RAII instrumentation ─────────────────────────");

    const int N = 50'000;
    std::vector<double> px(1024, 100.0), sz(1024, 100.0);
    double book[2]{};

    for (int i = 0; i < N; ++i) {
        validate_order(100.0 + i * 0.001, 100);
        calc_vwap(px.data(), sz.data(), 1024);
        update_book(book, 100.0 + i * 0.001, 100.0);
    }

    std::puts("  per-site latency summary:");
    for (int s = 0; s < 6; ++s)
        g_stats[s].print(g_stat_names[s]);
}

// ── PART 2: latency histogram ring buffer ─────────────────────────────
// Instead of a running accumulator, store the last N raw samples in a
// circular buffer.  Useful for computing p99 without sorting all data.
static void demo_ring_histogram()
{
    std::puts("\n── ring-buffer latency histogram ─────────────────");

    const int BUF_SIZE = 1024; // power-of-two
    std::array<std::int64_t, BUF_SIZE> ring{};
    int write_pos = 0;

    const int N = 10'000;
    for (int i = 0; i < N; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        volatile double x = 0;
        int loops = (i % 200 == 0) ? 20000 : 200;
        for (int j = 0; j < loops; ++j) x += j * 0.001;
        auto t1 = std::chrono::steady_clock::now();
        std::int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              t1 - t0).count();
        ring[static_cast<std::size_t>(write_pos++ & (BUF_SIZE - 1))] = ns;
        (void)x;
    }

    // Compute percentiles from the ring buffer snapshot.
    std::vector<std::int64_t> snapshot(ring.begin(), ring.end());
    std::sort(snapshot.begin(), snapshot.end());

    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p / 100.0 * BUF_SIZE);
        return snapshot[std::min(idx, snapshot.size() - 1)];
    };

    std::printf("  last %d samples — p50=%lld ns  p95=%lld ns  p99=%lld ns\n",
                BUF_SIZE,
                (long long)pct(50), (long long)pct(95), (long long)pct(99));
}

// ── PART 3: perf_event_open (Linux) — hardware counters ──────────────
#if HAVE_PERF_EVENT
static long perf_event_open(struct perf_event_attr* hw_event, pid_t pid,
                             int cpu, int group_fd, unsigned long flags)
{
    return ::syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static void demo_perf_event()
{
    std::puts("\n── perf_event_open: hardware counters (Linux) ────");

    struct perf_event_attr pe{};
    pe.type           = PERF_TYPE_HARDWARE;
    pe.size           = sizeof(struct perf_event_attr);
    pe.config         = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled       = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv     = 1;

    int fd = static_cast<int>(perf_event_open(&pe, 0, -1, -1, 0));
    if (fd == -1) {
        std::perror("  perf_event_open");
        std::puts("  (try: echo 0 > /proc/sys/kernel/perf_event_paranoid)");
        return;
    }

    ::ioctl(fd, PERF_EVENT_IOC_RESET,  0);
    ::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

    // Measure: how many instructions does a 1-million iteration loop cost?
    volatile long sum = 0;
    for (int i = 0; i < 1'000'000; ++i) sum += i;

    ::ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    std::uint64_t count = 0;
    ::read(fd, &count, sizeof(count));
    ::close(fd);

    std::printf("  1M loop instructions: %llu  (IPC hint: ~%.1f per iter)\n",
                (unsigned long long)count,
                static_cast<double>(count) / 1'000'000.0);
    (void)sum;
}
#else
static void demo_perf_event()
{
    std::puts("\n── perf_event_open: Linux only ──────────────────");
    std::puts("  On macOS use: xcrun xctrace record --template 'CPU Counters'");
    std::puts("  On Linux:     perf stat -e instructions,cycles,cache-misses ./app");
}
#endif

// ── PART 4: profiling tool guide ─────────────────────────────────────
static void print_tool_guide()
{
    std::puts("\n── profiling tool quick reference ────────────────");
    std::puts(
        "  macOS (Instruments):\n"
        "    xcrun xctrace record --template 'Time Profiler' --launch ./app\n"
        "    xcrun xctrace record --template 'Allocations'   --launch ./app\n"
        "\n"
        "  Linux (perf):\n"
        "    perf stat ./app                        # hardware counter summary\n"
        "    perf record -g ./app && perf report    # call-graph sampling\n"
        "    perf stat -e L1-dcache-misses,LLC-misses ./app\n"
        "\n"
        "  Both (valgrind):\n"
        "    valgrind --tool=callgrind ./app\n"
        "    callgrind_annotate callgrind.out.*\n"
        "\n"
        "  Linux (flamegraph):\n"
        "    perf record -F 99 -g ./app\n"
        "    perf script | stackcollapse-perf.pl | flamegraph.pl > out.svg"
    );
}

int main()
{
    demo_raii_probe();
    demo_ring_histogram();
    demo_perf_event();
    print_tool_guide();
}
