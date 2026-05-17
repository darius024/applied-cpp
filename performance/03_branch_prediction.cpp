#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Branch prediction: sorted vs unsorted, branchless patterns, [[likely]].
//
// The CPU's branch predictor guesses which path a conditional branch
// will take and starts executing speculatively.  On modern Intel/AMD:
//   - Correct prediction: ~0 penalty.
//   - Misprediction: ~10–20 cycles to flush the pipeline.
//
// Branches that predict well:
//   - Loop counters (always taken until the last iteration).
//   - Error checks that almost never fire (always not-taken).
//   - Branches over sorted data (stable patterns).
//
// Branches that predict poorly:
//   - Data-dependent branches over random/unsorted data.
//   - Virtual dispatch to many different derived types.
//   - Alternating patterns at irregular intervals.
//
// Classic benchmark: sum elements > threshold.
//   Sorted array → predictor locks onto "taken" then "not taken";
//   almost no mispredictions.
//   Random array  → ~50% mispredictions → much slower.
//
// Branchless patterns replace a branch with arithmetic/bitwise ops:
//   The compiler emits a CMOV (conditional move) — no pipeline flush.
//
// [[likely]] / [[unlikely]] (C++20)
//   Hint to the compiler which branch is the common case.
//   The compiler lays out the fast path as a not-taken branch
//   (cheaper on most CPUs) and moves the slow path to a cold section.
//
// Compile:  g++ -std=c++20 -O2 03_branch_prediction.cpp -o branch && ./branch
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static double elapsed_ns(std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b)
{
    return static_cast<double>(
               std::chrono::duration_cast<NS>(b - a).count());
}

// ── PART 1: sorted vs random branch ───────────────────────────────────
static void demo_sorted_vs_random()
{
    std::puts("── sorted vs random branch ──────────────────────");

    const int N    = 1 << 20; // 1 M elements
    const int REPS = 8;
    const int THRESHOLD = 128;

    std::vector<int> data(static_cast<std::size_t>(N));
    std::mt19937 rng{42};
    std::uniform_int_distribution<int> dist{0, 255};
    for (auto& v : data) v = dist(rng);

    std::vector<int> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end());

    // Unsorted — ~50% of branches are taken vs not-taken unpredictably.
    volatile long sink = 0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        long sum = 0;
        for (int i = 0; i < N; ++i)
            if (data[static_cast<std::size_t>(i)] > THRESHOLD)
                sum += data[static_cast<std::size_t>(i)];
        sink = sum;
    }
    auto t1 = Clock::now();
    double unsorted_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    // Sorted — first half always not-taken, second half always taken.
    // Predictor has near-100% accuracy after a brief warm-up.
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        long sum = 0;
        for (int i = 0; i < N; ++i)
            if (sorted_data[static_cast<std::size_t>(i)] > THRESHOLD)
                sum += sorted_data[static_cast<std::size_t>(i)];
        sink = sum;
    }
    t1 = Clock::now();
    double sorted_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    std::printf("  unsorted: %.3f ns/element\n", unsorted_ns);
    std::printf("  sorted:   %.3f ns/element\n", sorted_ns);
    std::printf("  speedup:  %.2fx  (branch predictor benefit)\n",
                unsorted_ns / sorted_ns);
    (void)sink;
}

// ── PART 2: branchless patterns ───────────────────────────────────────
// These patterns compile to CMOV / arithmetic instead of Jcc.
// Inspect with: g++ -O2 -S → look for cmovge, cmovl etc.
static void demo_branchless()
{
    std::puts("\n── branchless patterns ──────────────────────────");

    const int N    = 1 << 20;
    const int REPS = 8;
    std::vector<int> data(static_cast<std::size_t>(N));
    std::mt19937 rng{42};
    std::generate(data.begin(), data.end(),
                  [&]{ return static_cast<int>(rng()) - 1000; });

    // abs with branch
    volatile long s1 = 0, s2 = 0, s3 = 0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; ++i)
            s1 += (data[static_cast<std::size_t>(i)] < 0)
                  ? -data[static_cast<std::size_t>(i)]
                  :  data[static_cast<std::size_t>(i)];
    auto t1 = Clock::now();
    double branch_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    // abs branchless via bitwise (sign extension trick)
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; ++i) {
            int v    = data[static_cast<std::size_t>(i)];
            int mask = v >> 31; // -1 if negative, 0 if positive
            s2      += (v ^ mask) - mask;
        }
    t1 = Clock::now();
    double bless_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    // abs via std::abs — same result; compiler knows best
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; ++i)
            s3 += std::abs(data[static_cast<std::size_t>(i)]);
    t1 = Clock::now();
    double stdabs_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    std::printf("  abs (branch):       %.3f ns\n", branch_ns);
    std::printf("  abs (branchless):   %.3f ns\n", bless_ns);
    std::printf("  abs (std::abs):     %.3f ns\n", stdabs_ns);
    (void)s1; (void)s2; (void)s3;

    // Branchless clamp: often emitted as two CMOVs.
    std::puts("\n  branchless clamp demo:");
    auto clamp_bl = [](int v, int lo, int hi) noexcept -> int {
        // std::clamp compiles to CMOV with -O2
        return std::clamp(v, lo, hi);
    };
    for (int v : {-5, 0, 50, 100, 200})
        std::printf("    clamp(%4d, 0, 100) = %d\n", v, clamp_bl(v, 0, 100));
}

// ── PART 3: [[likely]] / [[unlikely]] ─────────────────────────────────
// C++20 attributes tell the compiler which branch is the common case.
// Effect: the common case becomes the fall-through (not-taken branch =
// no branch overhead), the rare case is a forward jump.

enum class RejectReason { None, PriceOob, SizeZero, DuplicateId };

struct Order { double price; int qty; int id; };

[[gnu::noinline]] // keep separate so we can see the effect
static RejectReason validate_order(const Order& o)
{
    // Negative price check — very rare in production.
    if (o.price <= 0.0) [[unlikely]]
        return RejectReason::PriceOob;

    // Zero quantity check — also rare.
    if (o.qty <= 0) [[unlikely]]
        return RejectReason::SizeZero;

    // Invalid ID — should never happen in practice.
    if (o.id <= 0) [[unlikely]]
        return RejectReason::DuplicateId;

    return RejectReason::None; // [[likely]] path — no attribute needed;
                               // the compiler defaults to assuming the
                               // last branch is the return path.
}

static void demo_likely_unlikely()
{
    std::puts("\n── [[likely]] / [[unlikely]] ────────────────────");

    const int N = 1 << 20;
    const int REPS = 4;
    std::vector<Order> orders(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i)
        orders[static_cast<std::size_t>(i)] = { 100.0 + i * 0.001, 100, i + 1 };

    volatile int accepted = 0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < N; ++i)
            if (validate_order(orders[static_cast<std::size_t>(i)])
                == RejectReason::None)
                ++accepted;
    auto t1 = Clock::now();
    double ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));
    std::printf("  validate_order: %.3f ns/call  accepted=%d\n",
                ns, (int)accepted);
    std::puts("  [[unlikely]] keeps error paths off the hot code cache.");
}

// ── PART 4: branch-free order book update ────────────────────────────
// Common in HFT: update best bid/offer without a branch on side.
// Use a 2-element array indexed by a 0/1 side flag.
static void demo_branchfree_book()
{
    std::puts("\n── branch-free book update ──────────────────────");

    struct Level { double price; int qty; };
    Level book[2] = {{ 99.9, 500 }, { 100.1, 500 }}; // [0]=bid [1]=ask

    // Update without branch: side ∈ {0=bid, 1=ask}
    auto update = [&](int side, double px, int qty) {
        book[side] = { px, qty };
    };

    update(0, 99.95, 300);
    update(1, 100.05, 200);

    std::printf("  bid: %.2f x %d\n", book[0].price, book[0].qty);
    std::printf("  ask: %.2f x %d\n", book[1].price, book[1].qty);
    std::puts("  (no if/else on the update path — indexed array)");
}

int main()
{
    demo_sorted_vs_random();
    demo_branchless();
    demo_likely_unlikely();
    demo_branchfree_book();
}
