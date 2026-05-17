#include <cstdio>
#include <cstdlib>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// AddressSanitizer — heap bugs: overflow, use-after-free, double-free.
//
// This file is SAFE to compile and run without sanitizers.
// Each bug section is guarded by a -D flag and shows what ASan reports.
//
// Compile (safe — shows only commentary):
//   g++ -std=c++20 -O1 -g 01_asan_heap.cpp -o asan_heap && ./asan_heap
//
// Compile + run WITH ASan (triggers each bug and reports):
//   g++ -std=c++20 -O1 -g -fsanitize=address,undefined \
//       -fno-omit-frame-pointer \
//       -DTRIGGER_OVERFLOW 01_asan_heap.cpp -o asan_heap && ./asan_heap
//
// Other triggers (one at a time — each crashes):
//   -DTRIGGER_UAF       use-after-free
//   -DTRIGGER_INIT_ORDER  global init order (enable with ASAN_OPTIONS=...)
//
// ASAN_OPTIONS reference:
//   ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:verbosity=1 ./asan_heap
//
// ASan mechanism:
//   malloc() allocates redzones (poisoned shadow bytes) before and after
//   every heap block.  Any access to a redzone triggers a report.
//   The shadow map covers the full address space at 1:8 ratio.
// ─────────────────────────────────────────────────────────────────────

// ── Safe helpers used across demos ───────────────────────────────────
static void section(const char* title)
{
    std::printf("\n── %s ───────────────────────────────────────\n", title);
}

// ── Bug 1: Heap buffer overflow ───────────────────────────────────────
//
// ASan report (with -DTRIGGER_OVERFLOW):
//   ERROR: AddressSanitizer: heap-buffer-overflow on address ...
//   WRITE of size 8 at ...
//   allocated by ... alloc_prices
//
// Root cause: array allocated for N doubles, but written at index N.
//
static void demo_heap_overflow()
{
    section("heap buffer overflow");
    const int N = 10;
    auto* prices = new double[N]; // [0..9] valid; [10] is redzone

    for (int i = 0; i < N; ++i) prices[i] = 100.0 + i;

#ifdef TRIGGER_OVERFLOW
    // OOB write — caught by ASan redzone:
    prices[N] = 999.0; // one past the end
    std::puts("  (ASan should have fired above this line)");
#else
    std::printf("  prices[0]=%.1f  prices[%d]=%.1f  (safe access)\n",
                prices[0], N - 1, prices[N - 1]);
    std::puts("  compile with -DTRIGGER_OVERFLOW to see ASan report");
#endif

    delete[] prices;
}

// ── Bug 2: Use-after-free (UAF) ───────────────────────────────────────
//
// ASan report (with -DTRIGGER_UAF):
//   ERROR: AddressSanitizer: heap-use-after-free on address ...
//   READ of size 8
//   freed by ... demo_uaf
//   previously allocated by ... demo_uaf
//
// Root cause: pointer used after the block was freed.
// Common in event-driven systems where callbacks outlive allocations.
//
static void demo_uaf()
{
    section("use-after-free");
    double* order_price = new double{101.50};
    std::printf("  price before free: %.2f\n", *order_price);

    delete order_price; // free the block

#ifdef TRIGGER_UAF
    // Dangling pointer read — caught by ASan quarantine:
    std::printf("  price after free (UAF!): %.2f\n", *order_price);
    std::puts("  (ASan should have fired above this line)");
#else
    order_price = nullptr; // defensive null — prevents UAF
    std::puts("  pointer nulled after free — no UAF possible");
    std::puts("  compile with -DTRIGGER_UAF to see ASan report");
#endif
}

// ── Bug 3: Heap underflow (write before allocation start) ─────────────
//
// ASan report:
//   heap-buffer-overflow ... WRITE of size 4
//   (the write is *before* the allocation start — left redzone)
//
static void demo_heap_underflow()
{
    section("heap underflow (write before start)");
    int* data = new int[8]{};

#ifdef TRIGGER_UNDERFLOW
    data[-1] = 42; // left redzone — caught by ASan
    std::puts("  (ASan should have fired above this line)");
#else
    std::printf("  data[0]=%d  (safe — no underflow)\n", data[0]);
    std::puts("  compile with -DTRIGGER_UNDERFLOW to see ASan report");
#endif

    delete[] data;
}

// ── Bug 4: Memory leak (caught by LeakSanitizer) ──────────────────────
//
// LSan report (Linux, or ASAN_OPTIONS=detect_leaks=1 on macOS):
//   Direct leak of 80 byte(s) in 1 object(s)
//   allocated by ... demo_leak
//
// Safe run: no report — no sanitizer active.
//
static void demo_leak()
{
    section("memory leak (LeakSanitizer)");
    auto* ticks = new double[10];
    for (int i = 0; i < 10; ++i) ticks[i] = 100.0 + i;
    std::printf("  allocated 10 doubles at %p (not freed)\n",
                static_cast<void*>(ticks));
    // ticks intentionally not deleted — LSan catches this at exit.
    std::puts("  run with ASAN_OPTIONS=detect_leaks=1 to see leak report");
    // To fix: delete[] ticks; or use std::vector<double>.
}

// ── Bug 5: Use-after-free via std::vector reallocation ────────────────
//
// Iterator invalidation is a subtle form of UAF.
// ASan catches it because the old backing buffer is freed after push_back.
//
#include <vector>
static void demo_iterator_invalidation()
{
    section("iterator invalidation (UAF pattern)");
    std::vector<double> book;
    book.reserve(4);
    book.push_back(100.0);
    const double* ptr = book.data(); // pointer into internal buffer

    for (int i = 0; i < 5; ++i) book.push_back(200.0 + i); // may reallocate!

#ifdef TRIGGER_ITER_INVAL
    // Dereferencing ptr after reallocation: stale pointer.
    std::printf("  old ptr: %.2f (UAF after realloc!)\n", *ptr);
#else
    (void)ptr;
    // Correct: use indices or re-fetch data() after modifications.
    std::printf("  book.data()[0]=%.1f  (accessed safely via fresh pointer)\n",
                book.data()[0]);
    std::puts("  compile with -DTRIGGER_ITER_INVAL to see ASan report");
#endif
}

int main()
{
    std::puts("=== ASan heap bugs ===");
    demo_heap_overflow();
    demo_uaf();
    demo_heap_underflow();
    demo_leak();
    demo_iterator_invalidation();
    std::puts("\n=== done (run with -fsanitize=address and -DTRIGGER_* flags) ===");
}
