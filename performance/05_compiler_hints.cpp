#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

// ─────────────────────────────────────────────────────────────────────
// Compiler hints: [[likely]]/[[unlikely]], always_inline, hot/cold,
// __builtin_expect, __builtin_unreachable, restrict, noinline.
//
// These are annotations that guide the compiler's code generation
// without changing the observable semantics of the program (apart
// from performance).  They are especially valuable on the critical
// path of an order-management or pricing system.
//
// [[likely]] / [[unlikely]]  (C++20, attributes on if/switch)
//   Tells the compiler which branch is the common case.
//   Effect: the common case becomes the fall-through (not-taken branch),
//   the rare case is a forward jump (or placed in a cold section).
//
// __builtin_expect(expr, expected_value)  (GCC/Clang extension)
//   The pre-C++20 way to do the same thing.  __builtin_expect(!!(x), 1)
//   is equivalent to if (x) [[likely]].
//
// [[gnu::always_inline]]  — force the compiler to inline regardless of
//   heuristics.  Use for tiny hot-path helpers where call overhead
//   matters (e.g. a 2-instruction byte-swap called millions of times).
//
// [[gnu::noinline]]  — prevent inlining.  Use for:
//   - Error paths (keep them out of the hot I-cache).
//   - Functions you want visible in profiler output.
//   - Preventing the benchmark SUT from being inlined into the harness.
//
// [[gnu::hot]]   — mark a function as hot; compiler clusters it with
//   other hot code in the text segment and applies extra optimisations.
//
// [[gnu::cold]]  — mark a function as cold; compiler moves it away from
//   hot code.  Helps I-cache utilisation.
//
// [[gnu::pure]]  — function has no side effects and depends only on
//   arguments + global memory (but not volatile).  Compiler may hoist
//   and CSE calls to it.
//
// [[gnu::const]] — even stricter: only depends on arguments; no reads
//   from global memory.  Compiler can eliminate identical calls.
//
// __builtin_unreachable()  — tells the compiler this point is never
//   reached.  Enables dead-code removal and avoids "missing return"
//   warnings.  If it IS reached, undefined behaviour.
//
// Compile:  g++ -std=c++20 -O2 05_compiler_hints.cpp -o hints && ./hints
// ─────────────────────────────────────────────────────────────────────

// ── always_inline: force inlining of a tiny hot-path helper ───────────
[[gnu::always_inline]] inline
static int fast_sign(int x) noexcept
{
    // Branchless sign: -1, 0, or +1.
    return (x > 0) - (x < 0);
}

// ── noinline: keep error handler off the hot path ─────────────────────
[[gnu::noinline, gnu::cold]]
static void handle_invalid_price(double price)
{
    std::fprintf(stderr, "  invalid price: %g\n", price);
    // In production: log and reject the order.
}

// ── hot: price calculator called on every tick ────────────────────────
[[gnu::hot]]
static double calc_mid(double bid, double ask) noexcept
{
    return (bid + ask) * 0.5;
}

// ── pure: only reads its arguments; no side effects ───────────────────
[[gnu::pure]]
static double point_value(int qty, double price) noexcept
{
    return static_cast<double>(qty) * price;
}

// ── const: depends only on arguments; not even global memory ──────────
[[gnu::const]]
static double tick_size(int venue) noexcept
{
    // Lookup table — result depends only on 'venue'.
    static const double ticks[] = { 0.01, 0.001, 0.0001, 0.00001 };
    return (venue >= 0 && venue < 4) ? ticks[venue] : 0.01;
}

// ── PART 1: [[likely]] on validation hot path ─────────────────────────
static void demo_likely()
{
    std::puts("── [[likely]] on validation ──────────────────────");

    auto validate = [](double bid, double ask, int qty) -> bool {
        // Error cases are [[unlikely]] — compiler puts them in cold code.
        if (bid <= 0.0)    [[unlikely]] { handle_invalid_price(bid); return false; }
        if (ask <= bid)    [[unlikely]] { handle_invalid_price(ask); return false; }
        if (qty <= 0)      [[unlikely]] { std::fputs("  bad qty\n", stderr); return false; }
        return true;       // [[likely]] implicit — fast path falls through
    };

    struct Case { double bid; double ask; int qty; };
    for (auto [bid, ask, qty] : std::initializer_list<Case>{
            { 100.0, 100.1, 100 },   // valid
            { -1.0,  100.1, 100 },   // bad bid
            { 100.0,  99.9, 100 },   // inverted
            { 100.0, 100.1,  -5 },   // bad qty
    }) {
        std::printf("  bid=%.1f ask=%.1f qty=%3d → %s\n",
                    bid, ask, qty, validate(bid, ask, qty) ? "OK" : "REJECTED");
    }
}

// ── PART 2: __builtin_expect — pre-C++20 equivalent ───────────────────
static void demo_builtin_expect()
{
    std::puts("\n── __builtin_expect ─────────────────────────────");

    // Equivalent to [[unlikely]] on the inner if.
    auto is_error = [](int code) -> bool {
        if (__builtin_expect(code != 0, 0)) { // 0 = not expected to be true
            std::printf("  error code: %d\n", code);
            return true;
        }
        return false;
    };

    for (int c : {0, 0, 0, 42, 0})
        is_error(c);
    std::puts("  (__builtin_expect is the pre-C++20 [[likely]] form)");
}

// ── PART 3: __builtin_unreachable for exhaustive switch ───────────────
// Removes the dead branch after the switch, suppresses warnings,
// and lets the compiler assume it is never reached.
enum class Side { Buy, Sell };

static double order_sign(Side s) noexcept
{
    switch (s) {
        case Side::Buy:  return  1.0;
        case Side::Sell: return -1.0;
    }
    __builtin_unreachable(); // compiler: no default path exists
}

static void demo_unreachable()
{
    std::puts("\n── __builtin_unreachable ─────────────────────────");
    std::printf("  Buy  → %+.1f\n", order_sign(Side::Buy));
    std::printf("  Sell → %+.1f\n", order_sign(Side::Sell));
}

// ── PART 4: always_inline vs noinline in practice ─────────────────────
// Micro-benchmark: accumulate fast_sign over a large array.
// [[gnu::always_inline]] ensures no call overhead; the body is just two
// subtractions and a subtract.

[[gnu::noinline]] // keep visible in profiler
static long accumulate_signs(const int* data, int n)
{
    long sum = 0;
    for (int i = 0; i < n; ++i)
        sum += fast_sign(data[i]); // always_inline — no call overhead
    return sum;
}

static void demo_inline()
{
    std::puts("\n── always_inline / noinline demo ─────────────────");
    const int N = 1 << 20;
    int* data = new int[N];
    for (int i = 0; i < N; ++i) data[i] = i - N / 2;
    long s = accumulate_signs(data, N);
    std::printf("  sum of signs over %d values = %ld\n", N, s);
    delete[] data;
}

// ── PART 5: pure/const — call-site CSE ───────────────────────────────
static void demo_pure_const()
{
    std::puts("\n── [[gnu::pure]] / [[gnu::const]] ───────────────");
    // With [[gnu::const]], identical calls to tick_size(0) will be
    // computed once and reused (common subexpression elimination).
    for (int v = 0; v < 4; ++v)
        std::printf("  tick_size(%d) = %.5f  |  point_value(100, %.2f) = %.2f\n",
                    v, tick_size(v), 99.5 + v, point_value(100, 99.5 + v));
}

int main()
{
    demo_likely();
    demo_builtin_expect();
    demo_unreachable();
    demo_inline();
    demo_pure_const();

    std::puts("\n── calc_mid demo ─────────────────────────────────");
    std::printf("  mid(99.9, 100.1) = %.4f\n", calc_mid(99.9, 100.1));
}
