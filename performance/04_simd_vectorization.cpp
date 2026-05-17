#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// SIMD auto-vectorization: conditions, __restrict__, assume_aligned.
//
// SIMD (Single Instruction, Multiple Data): the CPU has 256-bit (AVX)
// or 512-bit (AVX-512) registers that can hold 4 or 8 doubles at once.
// Processing N=4 elements per instruction gives ≤4× throughput gain.
//
// The compiler AUTO-vectorizes at -O2/-O3 when it can prove:
//
//   1. No aliasing — the source and destination pointers do not overlap.
//      Solution: use `__restrict__` (or `restrict` in C99) to assert
//      that pointers do not alias.  std::assume_aligned (C++20) asserts
//      alignment, enabling aligned loads (faster on some CPUs).
//
//   2. No data dependency between iterations.
//      Loop-carried dependency (out[i] = f(out[i-1])) → NOT vectorizable.
//      Independent loops (out[i] = a[i] + b[i]) → vectorizable.
//
//   3. Contiguous memory access with known stride.
//      Scatter/gather (indirect indexing) → harder to vectorize.
//
//   4. Sufficient iteration count — short loops may not be worth it.
//
// How to check: add -fopt-info-vec-optimized (GCC) or
//   -Rpass=loop-vectorize (Clang) to the compile command.
//
// Compile:
//   g++ -std=c++20 -O3 -fopt-info-vec-optimized \
//       04_simd_vectorization.cpp -o simd && ./simd
//
//   Or with Clang:
//   clang++ -std=c++20 -O3 -Rpass=loop-vectorize \
//       04_simd_vectorization.cpp -o simd && ./simd
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static double elapsed_ns(std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b)
{
    return static_cast<double>(
               std::chrono::duration_cast<NS>(b - a).count());
}

// ── PART 1: simple loop — should auto-vectorize ───────────────────────
// Compiler sees: out[i] = a[i] + b[i], independent iterations, no alias.

// Without restrict — compiler inserts runtime alias check (peel loop).
static void add_no_restrict(const double* a, const double* b,
                            double* out, int n)
{
    for (int i = 0; i < n; ++i)
        out[i] = a[i] + b[i];
}

// With __restrict__ — compiler trusts no overlap; no peel loop needed.
static void add_restrict(const double* __restrict__ a,
                         const double* __restrict__ b,
                         double* __restrict__ out, int n)
{
    for (int i = 0; i < n; ++i)
        out[i] = a[i] + b[i];
}

// ── PART 2: loop-carried dependency — NOT vectorizable ─────────────────
// Each iteration depends on the previous result.
// The compiler must keep this scalar.
static double running_pnl(const double* deltas, int n)
{
    double pnl = 0.0;
    for (int i = 0; i < n; ++i)
        pnl += deltas[i]; // reduction — can be vectorized with horizontal add
    return pnl;
}

// True loop-carried: prefix_sum (scan) — cannot vectorize naively.
static void prefix_sum(const double* __restrict__ in,
                       double* __restrict__ out, int n)
{
    out[0] = in[0];
    for (int i = 1; i < n; ++i)
        out[i] = out[i - 1] + in[i]; // depends on out[i-1]
}

// ── PART 3: std::assume_aligned (C++20) ───────────────────────────────
// Tells the compiler the pointer is aligned to N bytes.
// Removes the peel loop for alignment; enables pure aligned SIMD loads.

static void add_aligned(const double* a_raw, const double* b_raw,
                        double* out_raw, int n)
{
    // Assert 32-byte (256-bit AVX) alignment — undefined behaviour if wrong!
    const double* a   = std::assume_aligned<32>(a_raw);
    const double* b   = std::assume_aligned<32>(b_raw);
    double*       out = std::assume_aligned<32>(out_raw);
    for (int i = 0; i < n; ++i)
        out[i] = a[i] + b[i]; // aligned SIMD load/store
}

// ── PART 4: VWAP using vectorized multiply-add ─────────────────────────
// sum(price[i] * qty[i]) and sum(qty[i]) — both vectorizable.
static double vwap(const double* __restrict__ price,
                   const double* __restrict__ qty, int n)
{
    double num = 0.0, den = 0.0;
    for (int i = 0; i < n; ++i) {
        num += price[i] * qty[i]; // FMA: fused multiply-add (on -O3 -mfma)
        den += qty[i];
    }
    return den > 0.0 ? num / den : 0.0;
}

int main()
{
    const int N    = 1 << 20; // 1 M elements
    const int REPS = 8;

    // Allocate 32-byte aligned buffers for AVX.
    auto alloc32 = [&](int n) -> double* {
        void* p = nullptr;
        if (::posix_memalign(&p, 32, static_cast<std::size_t>(n) * sizeof(double)) != 0)
            return nullptr;
        return static_cast<double*>(p);
    };

    double* a   = alloc32(N);
    double* b   = alloc32(N);
    double* out = alloc32(N);
    for (int i = 0; i < N; ++i) {
        a[i]   = 1.0 + i * 0.001;
        b[i]   = 2.0 - i * 0.0005;
        out[i] = 0.0;
    }

    // ── Benchmark: no-restrict vs restrict vs assume_aligned ────────────
    std::puts("── vectorization: add_no_restrict vs add_restrict ─");

    volatile double sink = 0.0;
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        add_no_restrict(a, b, out, N);
        sink = out[N / 2];
    }
    auto t1 = Clock::now();
    double no_res = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        add_restrict(a, b, out, N);
        sink = out[N / 2];
    }
    t1 = Clock::now();
    double with_res = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        add_aligned(a, b, out, N);
        sink = out[N / 2];
    }
    t1 = Clock::now();
    double with_align = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    std::printf("  add_no_restrict:  %.3f ns/element\n", no_res);
    std::printf("  add_restrict:     %.3f ns/element\n", with_res);
    std::printf("  add_aligned:      %.3f ns/element\n", with_align);

    // ── VWAP benchmark ────────────────────────────────────────────────
    std::puts("\n── VWAP: vectorized multiply-accumulate ──────────");
    for (int i = 0; i < N; ++i) { a[i] = 100.0 + i * 0.01; b[i] = 100.0; }

    double result = 0.0;
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r)
        result = vwap(a, b, N);
    t1 = Clock::now();
    double vwap_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));
    std::printf("  VWAP result: %.4f  (%.3f ns/element)\n", result, vwap_ns);

    // ── Scalar reduction via std::accumulate for comparison ────────────
    std::vector<double> vec_a(a, a + N);
    std::puts("\n── loop-carried dependency: prefix_sum ──────────");
    double* ps_out = alloc32(N);
    prefix_sum(a, ps_out, N);
    std::printf("  prefix_sum[%d] = %.2f  (loop-carried; scalar)\n",
                N - 1, ps_out[N - 1]);

    std::puts("\n── compile with -fopt-info-vec-optimized to see  ─");
    std::puts("   which loops were actually vectorized by the compiler.");

    ::free(a); ::free(b); ::free(out); ::free(ps_out);
    (void)sink;
}
