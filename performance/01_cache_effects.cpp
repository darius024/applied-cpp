#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Cache effects: sequential vs random access, false sharing, AoS vs SoA.
//
// The CPU cache operates in 64-byte lines.  Accessing any byte within
// a line loads the entire line.  This has two consequences:
//
//   1) Sequential access (stride 1) — each cache line feeds 8 doubles;
//      hardware prefetching kicks in and hides most of the latency.
//
//   2) Random access (large stride or pointer chasing) — each element
//      is in a different cache line; every access is a cache miss.
//      At ~200 cycles/miss × millions of accesses = seconds of waiting.
//
// False sharing:
//   Two threads modify different variables that share the same cache
//   line.  Each write broadcasts an "invalidate" to the other core,
//   forcing a reload on the next access — cache-line ping-pong.
//   Fix: pad or align each hot variable to 64 bytes.
//
// AoS vs SoA:
//   Processing one field across N quotes in SoA layout uses every byte
//   loaded.  In AoS layout, loading quote[i].bid also loads .ask/.mid/.sz
//   which are unused — wasted bandwidth.
//
// Compile:  g++ -std=c++20 -O2 01_cache_effects.cpp -o cache && ./cache
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static double elapsed_ns(Clock::time_point a, Clock::time_point b)
{
    return static_cast<double>(
               std::chrono::duration_cast<NS>(b - a).count());
}

// ── PART 1: sequential vs stride access ──────────────────────────────
static void demo_access_patterns()
{
    std::puts("── sequential vs random access ──────────────────");

    const int N    = 1 << 22; // 4 M doubles = 32 MiB — exceeds L3
    const int REPS = 4;

    std::vector<double> data(static_cast<std::size_t>(N), 1.0);

    // ── Sequential: stride 1 ─────────────────────────────────────────
    volatile double sink = 0.0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; ++i)
            sink += data[static_cast<std::size_t>(i)];
    auto t1 = Clock::now();
    double seq_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    // ── Stride 16 (skip 15 doubles = 120 bytes ≈ every 2nd cache line) ─
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; i += 16)
            sink += data[static_cast<std::size_t>(i)];
    t1 = Clock::now();
    double str_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N / 16));

    // ── Random: pointer-chased shuffle ───────────────────────────────
    std::vector<int> idx(static_cast<std::size_t>(N));
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng{42};
    std::shuffle(idx.begin(), idx.end(), rng);

    t0 = Clock::now();
    for (int r = 0; r < REPS / 2; ++r)
        for (int i = 0; i < N; ++i)
            sink += data[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
    t1 = Clock::now();
    double rnd_ns = elapsed_ns(t0, t1) /
                    (REPS / 2 * static_cast<double>(N));

    std::printf("  sequential:  %.2f ns/element\n", seq_ns);
    std::printf("  stride-16:   %.2f ns/element\n", str_ns);
    std::printf("  random:      %.2f ns/element\n", rnd_ns);
    std::printf("  random/seq ratio: %.1fx\n", rnd_ns / seq_ns);
    (void)sink;
}

// ── PART 2: false sharing ─────────────────────────────────────────────
// Two atomics in the same cache line vs two separate cache lines.
// We simulate the effect single-threadedly by showing that padding
// a counter to 64 bytes eliminates the 'bounce' cost in multi-core
// scenarios.  The struct layout is the important lesson here.
static void demo_false_sharing_layout()
{
    std::puts("\n── false sharing — struct layout ────────────────");

    // BAD: x and y share one 64-byte cache line.
    struct Shared {
        std::atomic<long> x{0};
        std::atomic<long> y{0};
    };

    // GOOD: each counter on its own cache line.
    struct Padded {
        alignas(64) std::atomic<long> x{0};
        alignas(64) std::atomic<long> y{0};
    };

    std::printf("  Shared size  = %zu bytes (both in one cache line)\n",
                sizeof(Shared));
    std::printf("  Padded size  = %zu bytes (each on own cache line)\n",
                sizeof(Padded));

    // Demonstrate that x and y in Shared live in the same line.
    Shared s;
    auto* px = reinterpret_cast<char*>(&s.x);
    auto* py = reinterpret_cast<char*>(&s.y);
    std::printf("  &s.x = %p,  &s.y = %p  (diff = %td bytes)\n",
                static_cast<void*>(px), static_cast<void*>(py), py - px);

    Padded p;
    auto* qx = reinterpret_cast<char*>(&p.x);
    auto* qy = reinterpret_cast<char*>(&p.y);
    std::printf("  &p.x = %p,  &p.y = %p  (diff = %td bytes)\n",
                static_cast<void*>(qx), static_cast<void*>(qy), qy - qx);
    std::puts("  (diff == 64 means separate cache lines — no sharing)");
}

// ── PART 3: AoS vs SoA — VWAP calculation ────────────────────────────
static const int N_QUOTES = 1 << 18; // 256 K quotes

// Array of Structures — typical OOP layout.
struct QuoteAoS {
    double bid;
    double ask;
    double mid;
    int    size;
};

// Structure of Arrays — cache-friendly for field-level scans.
struct QuoteBookSoA {
    std::vector<double> bid;
    std::vector<double> ask;
    std::vector<double> mid;
    std::vector<int>    size;
};

static void demo_aos_vs_soa()
{
    std::puts("\n── AoS vs SoA — VWAP scan ───────────────────────");

    // Fill AoS
    std::vector<QuoteAoS> aos(N_QUOTES);
    for (int i = 0; i < N_QUOTES; ++i)
        aos[static_cast<std::size_t>(i)] = { 100.0 + i * 0.01,
                                              100.02 + i * 0.01,
                                              100.01 + i * 0.01, 100 };

    // Fill SoA
    QuoteBookSoA soa;
    soa.bid.resize(N_QUOTES);
    soa.ask.resize(N_QUOTES);
    soa.mid.resize(N_QUOTES);
    soa.size.resize(N_QUOTES);
    for (int i = 0; i < N_QUOTES; ++i) {
        soa.bid[static_cast<std::size_t>(i)]  = 100.0  + i * 0.01;
        soa.ask[static_cast<std::size_t>(i)]  = 100.02 + i * 0.01;
        soa.mid[static_cast<std::size_t>(i)]  = 100.01 + i * 0.01;
        soa.size[static_cast<std::size_t>(i)] = 100;
    }

    const int REPS = 8;

    // VWAP(bid) over AoS — loads bid, ask, mid, size; only uses bid+size.
    volatile double sink = 0.0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        double vwap = 0.0; long vol = 0;
        for (int i = 0; i < N_QUOTES; ++i) {
            const auto& q = aos[static_cast<std::size_t>(i)];
            vwap += q.bid * q.size;
            vol  += q.size;
        }
        sink = vol > 0 ? vwap / static_cast<double>(vol) : 0.0;
    }
    auto t1 = Clock::now();
    double aos_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N_QUOTES));

    // VWAP(bid) over SoA — loads only bid[] and size[]; 100% utilisation.
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        double vwap = 0.0; long vol = 0;
        for (int i = 0; i < N_QUOTES; ++i) {
            auto sz = soa.size[static_cast<std::size_t>(i)];
            vwap   += soa.bid[static_cast<std::size_t>(i)] * sz;
            vol    += sz;
        }
        sink = vol > 0 ? vwap / static_cast<double>(vol) : 0.0;
    }
    t1 = Clock::now();
    double soa_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N_QUOTES));

    std::printf("  AoS VWAP: %.3f ns/element\n", aos_ns);
    std::printf("  SoA VWAP: %.3f ns/element\n", soa_ns);
    std::printf("  SoA speedup: %.2fx\n", aos_ns / soa_ns);
    (void)sink;
}

int main()
{
    demo_access_patterns();
    demo_false_sharing_layout();
    demo_aos_vs_soa();
}
