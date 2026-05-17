#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// MemorySanitizer (MSan) — uninitialised memory reads.
//
// MSan is *Clang-only* and currently only supported on Linux/x86_64.
// It tracks "taint" (uninitialised status) for every byte in memory.
// A byte is tainted if it has never been written.  When a tainted byte
// influences a condition or is passed to an external function, MSan fires.
//
// Key constraint: ALL code in the process must be compiled with MSan,
// including the C++ standard library.  MSan cannot reason about
// uninstrumented code and produces false positives otherwise.
// In practice, build against a pre-compiled MSan-instrumented libc++:
//   clang++ -std=c++20 -O1 -g -fsanitize=memory \
//       -stdlib=libc++ -L/path/to/msan-libc++ \
//       05_msan.cpp -o msan && ./msan
//
// For this demo file we compile normally and annotate what MSan would
// report.  To actually trigger reports, use the -DTRIGGER_* flags with
// the Clang MSan build above.
//
// Compile (safe, annotated output):
//   g++ -std=c++20 -O1 -g 05_msan.cpp -o msan && ./msan
//
// Compile with MSan (Clang, Linux only):
//   clang++ -std=c++20 -O1 -g -fsanitize=memory -fno-omit-frame-pointer \
//           -DTRIGGER_UNINIT_COND \
//           05_msan.cpp -o msan && ./msan
//
// MSan report:
//   WARNING: MemorySanitizer: use-of-uninitialized-value
//     Uninitialized value was created by a stack allocation
//     in function demo_uninit_condition
//
// Runtime options:
//   MSAN_OPTIONS=halt_on_error=0:print_stats=1 ./msan
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// MSan helper: manually mark a region as initialised (use when you know
// external code (e.g., read(2)) has written to a buffer but MSan can't
// see the write because it's in uninstrumented libc).
// In real code: #include <sanitizer/msan_interface.h>
#if defined(__has_include) && __has_include(<sanitizer/msan_interface.h>)
#  include <sanitizer/msan_interface.h>
#  define MSAN_UNPOISON(p, n)  __msan_unpoison(p, n)
#else
#  define MSAN_UNPOISON(p, n)  (void)0
#endif

// ── Bug 1: uninitialised value used in a condition ─────────────────────
//
// MSan report:
//   use-of-uninitialized-value
//   Condition is based on uninitialised value.
//
static void demo_uninit_condition()
{
    section("uninitialised value in condition");
    int flag; // declared but never initialised

#ifdef TRIGGER_UNINIT_COND
    // MSan fires here: 'flag' has never been written.
    if (flag > 0)
        std::puts("  flag is positive (UB — uninitialised)");
    else
        std::puts("  flag is not positive (UB — uninitialised)");
#else
    int safe_flag = 0; // explicitly initialised
    if (safe_flag > 0)
        std::puts("  flag is positive");
    else
        std::puts("  flag is not positive (safe_flag=0 — properly initialised)");
    (void)flag;
    std::puts("  compile with -DTRIGGER_UNINIT_COND and -fsanitize=memory");
#endif
}

// ── Bug 2: uninitialised struct field ─────────────────────────────────
//
// Partially initialised structs are a common source of MSan reports.
// All fields must be explicitly initialised (or use = {} / memset).
//
// MSan report:
//   use-of-uninitialized-value
//   Uninitialized value was created by an allocation
//
struct Order {
    int    id;
    double price;
    int    qty;
    int    side; // 0=buy, 1=sell
};

static void process_order(const Order& o)
{
    // If any field was uninitialised, MSan fires here on use.
    std::printf("  order id=%d price=%.2f qty=%d side=%s\n",
                o.id, o.price, o.qty,
                o.side == 0 ? "buy" : "sell");
}

static void demo_uninit_struct()
{
    section("partially uninitialised struct");

    {
        Order bad;   // all fields uninitialised
        bad.id    = 42;
        bad.price = 101.5;
        bad.qty   = 100;
        // bad.side never set!

#ifdef TRIGGER_UNINIT_STRUCT
        process_order(bad); // MSan fires: bad.side is uninitialised
#else
        (void)bad;
        std::puts("  bad.side never set — compile with -DTRIGGER_UNINIT_STRUCT");
#endif
    }

    {
        Order good{};  // value-initialised: all fields = 0
        good.id    = 42;
        good.price = 101.5;
        good.qty   = 100;
        good.side  = 1;
        process_order(good); // safe
    }
}

// ── Bug 3: uninitialised memory after malloc ───────────────────────────
//
// malloc() does NOT zero-initialise.  calloc() does.
// MSan tracks malloc-allocated memory as uninitialised.
//
// MSan report:
//   use-of-uninitialized-value
//   from alloc in demo_malloc_uninit
//
#include <cstdlib>
static void demo_malloc_uninit()
{
    section("malloc vs calloc vs explicit init");

    // malloc: memory is uninitialised — any read is UB / MSan-caught.
    double* buf_bad = static_cast<double*>(std::malloc(8 * sizeof(double)));
    if (!buf_bad) return;

    // calloc: zero-initialised — safe to read.
    double* buf_good = static_cast<double*>(std::calloc(8, sizeof(double)));
    if (!buf_good) { std::free(buf_bad); return; }

    // MSan unpoison: use when external (uninstrumented) code initialises.
    MSAN_UNPOISON(buf_bad, 8 * sizeof(double));

#ifdef TRIGGER_MALLOC_UNINIT
    // Without the unpoison above, this would fire:
    std::printf("  buf_bad[0] = %.1f  (uninitialised — MSan fires)\n",
                buf_bad[0]);
#else
    // Write before read — always correct:
    for (int i = 0; i < 8; ++i) buf_bad[i] = 100.0 + i;
    std::printf("  buf_bad[0]  = %.1f (written before read — safe)\n",  buf_bad[0]);
    std::printf("  buf_good[0] = %.1f (calloc zero-init — safe)\n",     buf_good[0]);
#endif

    std::free(buf_bad);
    std::free(buf_good);
}

// ── Bug 4: uninitialised value propagation ─────────────────────────────
//
// MSan tracks taint through arithmetic.  If an uninitialised int is
// added to another value, the result is also tainted.
//
static void demo_taint_propagation()
{
    section("uninitialised taint propagation");

    int a = 5;
    int b;        // uninitialised
    int c = a + b; // c is tainted — b was uninitialised
    (void)c;

    // MSan fires when 'c' is used in a branch or passed externally.
    // Safe version:
    int b_safe = 0;
    int c_safe = a + b_safe;
    std::printf("  c_safe = %d  (all values initialised)\n", c_safe);
    std::puts("  in real build: 'c = a + b' with b uninitialised taints c");
    std::puts("  MSan fires when c is used in a condition or printed");
}

// ── Note on macOS ──────────────────────────────────────────────────────
static void print_platform_note()
{
    section("platform notes");
#ifdef __APPLE__
    std::puts("  MSan is NOT supported on macOS.");
    std::puts("  Alternatives:");
    std::puts("    - valgrind --tool=memcheck (slower but works on macOS)");
    std::puts("    - Use a Linux VM/container for MSan builds");
    std::puts("    - Clang static analyser: clang++ --analyze");
#elif defined(__linux__)
    std::puts("  Linux detected — MSan is available with Clang.");
    std::puts("  Remember: rebuild libc++ and all deps with -fsanitize=memory");
    std::puts("  or use a pre-built MSan sysroot.");
#else
    std::puts("  MSan: check Clang documentation for platform support.");
#endif
}

int main()
{
    std::puts("=== MemorySanitizer (MSan) — uninitialised reads ===");
    demo_uninit_condition();
    demo_uninit_struct();
    demo_malloc_uninit();
    demo_taint_propagation();
    print_platform_note();
    std::puts("\n=== compile with clang++ -fsanitize=memory (Linux) ===");
}
