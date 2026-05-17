#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Build modes: Debug vs Release vs RelWithDebInfo, NDEBUG, assert,
// optimisation levels, and LTO effects.
//
// Compile:
//   Debug:          g++ -std=c++20 -O0 -g    08_build_modes.cpp -o modes_dbg && ./modes_dbg
//   Release:        g++ -std=c++20 -O3 -DNDEBUG 08_build_modes.cpp -o modes_rel && ./modes_rel
//   RelWithDebInfo: g++ -std=c++20 -O2 -g -DNDEBUG 08_build_modes.cpp -o modes_rwdi && ./modes_rwdi
//
// Compiler macros to query the build:
//   __OPTIMIZE__        defined when any optimisation is active
//   NDEBUG              defined when assert() is disabled (Release builds)
//   __GNUC__            GCC or Clang
//   __clang__           Clang specifically
//   __clang_version__   Clang version string
//   __GNUC_MINOR__      GCC minor version
//
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── PART 1: NDEBUG and assert ─────────────────────────────────────────
//
// assert(cond) expands to a runtime check in Debug (-g, no NDEBUG),
// and to nothing in Release (-DNDEBUG).
// Use assert for programmer invariants (not for user input validation).
//
// Rule: NEVER put side effects inside assert():
//   assert(pop_queue(q) != nullptr);  // BAD: pop doesn't happen in Release
//   auto* item = pop_queue(q); assert(item != nullptr);  // GOOD
//
static void demo_assert()
{
    section("NDEBUG and assert()");

#ifdef NDEBUG
    std::puts("  NDEBUG is defined — asserts are compiled out (Release mode)");
#else
    std::puts("  NDEBUG not defined — asserts are active (Debug mode)");
#endif

    // Invariant assert (programmer contract, not user input):
    std::vector<int> data{1, 2, 3};
    assert(!data.empty()); // compiled out in Release
    std::printf("  data[0] = %d  (assert checked in Debug only)\n", data[0]);

    // Custom conditional check that survives Release:
    auto safe_front = [&](const std::vector<int>& v) -> int {
        if (v.empty()) { std::puts("  BUG: empty vector!"); return 0; }
        return v.front();
    };
    std::printf("  safe_front = %d  (runs in both Debug and Release)\n",
                safe_front(data));
}

// ── PART 2: __OPTIMIZE__ — detect optimisation at compile time ────────
static void demo_optimize_macro()
{
    section("__OPTIMIZE__ macro");

#ifdef __OPTIMIZE__
    std::printf("  __OPTIMIZE__ is defined — compiler is optimising (-%c or higher)\n",
#  ifdef __OPTIMIZE_SIZE__
                's'
#  else
                '1'
#  endif
    );
#else
    std::puts("  __OPTIMIZE__ not defined — compiled with -O0 (Debug)");
#endif

#if defined(__clang__)
    std::printf("  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    std::printf("  Compiler: GCC %d.%d\n", __GNUC__, __GNUC_MINOR__);
#elif defined(_MSC_VER)
    std::printf("  Compiler: MSVC %d\n", _MSC_VER);
#endif
}

// ── PART 3: measurable impact of optimisation levels ─────────────────
//
// Computes a tight loop and times it.  With -O0 the loop body is
// not vectorised or unrolled.  With -O3 it typically auto-vectorises.
//
[[gnu::noinline]]
static double heavy_compute(int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(i) * 1.00001; // prevents folding
    return sum;
}

static void demo_opt_timing()
{
    section("optimisation level timing");

    const int N = 10'000'000;
    auto t0 = std::chrono::steady_clock::now();
    volatile double result = heavy_compute(N);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("  heavy_compute(%d) = %.1f  time=%.2f ms\n", N, (double)result, ms);

#ifdef __OPTIMIZE__
    std::puts("  (compiled optimised — expect fast result)");
#else
    std::puts("  (compiled -O0 — expect slower than Release)");
#endif
}

// ── PART 4: LTO — what changes at link time ───────────────────────────
//
// With LTO, the linker sees all object files' IR simultaneously.
// Functions that are only called from one place across TU boundaries
// can be inlined.  Dead functions are eliminated.
//
// Enable LTO:
//   GCC:   g++ -O2 -flto=auto main.cpp lib.cpp -o app
//   Clang: clang++ -O2 -flto=thin main.cpp lib.cpp -o app
//   CMake: set_property(TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
//
// Observable effects (not directly visible in code, but measurable):
//   - Smaller binary: dead cross-TU functions eliminated
//   - Faster: cross-TU inlining of small functions
//   - More devirtualisation: whole-program class hierarchy visible
//
// This function simulates a TU-boundary call that LTO can inline.
[[gnu::noinline]] // remove noinline to let the compiler inline without LTO
static double tick_value(double price, int qty)
{
    return price * static_cast<double>(qty);
}

static void demo_lto_concept()
{
    section("LTO concept");
    double tv = tick_value(101.5, 100); // cross-TU call in real projects
    std::printf("  tick_value = %.2f\n", tv);
    std::puts("  With LTO: tick_value() can be inlined from a separate TU");
    std::puts("  Flags: g++ -O2 -flto=auto; clang++ -O2 -flto=thin");
    std::puts("  CMake:  set_property(TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)");
}

// ── PART 5: debug-only logging pattern ───────────────────────────────
//
// A macro that produces zero overhead in Release builds.
//
#ifndef NDEBUG
#  define DBG_LOG(fmt, ...) std::printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#  define DBG_LOG(fmt, ...) (void)0
#endif

static void demo_debug_logging()
{
    section("debug-only logging (zero overhead in Release)");
    int order_id = 42;
    DBG_LOG("processing order %d", order_id);
    std::printf("  order %d processed  (debug log above only in Debug build)\n",
                order_id);
}

// ── PART 6: RelWithDebInfo ────────────────────────────────────────────
//
// -O2 -g -DNDEBUG
//
// Gives profiler/debugger-friendly binaries that still run at -O2 speed.
// Stack traces are human-readable.  Asserts are disabled.
//
// Split DWARF (-gsplit-dwarf): stores debug info in a separate .dwo file.
// The main binary is small; attach the .dwo for debugging.
//
// Flags:
//   g++ -O2 -g -DNDEBUG -gsplit-dwarf main.cpp -o app
//   objcopy --only-keep-debug app app.dbg && strip app
//   objcopy --add-gnu-debuglink=app.dbg app
//
static void demo_relwithdebinfo()
{
    section("RelWithDebInfo / split DWARF");
    std::puts("  -O2 -g -DNDEBUG: Release speed + debug symbols");
    std::puts("  -gsplit-dwarf  : debug info in separate .dwo file");
    std::puts("  strip          : remove debug info from production binary");
    std::puts("  debuglink      : gdb/lldb finds .dwo via embedded link");
}

int main()
{
    std::puts("=== build modes ===");
    demo_assert();
    demo_optimize_macro();
    demo_opt_timing();
    demo_lto_concept();
    demo_debug_logging();
    demo_relwithdebinfo();
    std::puts("\n=== compare: -O0 -g  vs  -O3 -DNDEBUG ===");
}
