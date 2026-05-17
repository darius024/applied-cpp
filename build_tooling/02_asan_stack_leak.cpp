#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// AddressSanitizer — stack bugs: stack overflow, use-after-scope,
// use-after-return.  LeakSanitizer: memory never freed at exit.
//
// Compile (safe, no sanitizer):
//   g++ -std=c++20 -O1 -g 02_asan_stack_leak.cpp -o asan_stack && ./asan_stack
//
// Compile with ASan (triggers -DTRIGGER_* flags):
//   g++ -std=c++20 -O1 -g -fsanitize=address,undefined \
//       -fno-omit-frame-pointer -fsanitize-address-use-after-scope \
//       -DTRIGGER_STACK_OVF 02_asan_stack_leak.cpp -o asan_stack && ./asan_stack
//
// Flags to try one at a time:
//   -DTRIGGER_STACK_OVF       stack buffer overflow
//   -DTRIGGER_USE_AFTER_SCOPE  use-after-scope (pointer escapes block)
//   -DTRIGGER_LEAK            memory leak at exit
//
// For use-after-return: requires runtime option:
//   ASAN_OPTIONS=detect_stack_use_after_return=1 ./asan_stack
//
// ASan stack mechanism:
//   Stack variables are surrounded by poisoned redzones.
//   On scope exit, the stack variable itself is poisoned.
//   Any subsequent access (e.g., via a dangling pointer) is caught.
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── Bug 1: Stack buffer overflow ──────────────────────────────────────
//
// ASan report (with -DTRIGGER_STACK_OVF):
//   ERROR: AddressSanitizer: stack-buffer-overflow on address ...
//   WRITE of size 8 at ... (1 byte to the right of ... 'prices')
//
static void demo_stack_overflow()
{
    section("stack buffer overflow");
    double prices[8]; // valid indices 0..7

    for (int i = 0; i < 8; ++i) prices[i] = 100.0 + i;

#ifdef TRIGGER_STACK_OVF
    prices[8] = 999.0; // one past end of stack array — right redzone
    std::puts("  (ASan should have fired above this line)");
#else
    std::printf("  prices[7]=%.1f (last valid element)\n", prices[7]);
    std::puts("  compile with -DTRIGGER_STACK_OVF to see ASan report");
#endif
}

// ── Bug 2: Use-after-scope ────────────────────────────────────────────
//
// Requires: -fsanitize-address-use-after-scope (included in ASan since
// Clang 3.7 / GCC 7, not default — must be explicit).
//
// ASan report (with -DTRIGGER_USE_AFTER_SCOPE):
//   ERROR: AddressSanitizer: stack-use-after-scope on address ...
//   READ of size 8
//   is inside variable 'local_price' declared at ...
//
static double* g_dangling = nullptr; // global stash for the dangling ptr

static void fill_dangling()
{
    double local_price = 101.75;
    g_dangling = &local_price; // pointer to stack variable escapes!
} // local_price destroyed here — g_dangling now dangling

static void demo_use_after_scope()
{
    section("use-after-scope");
    fill_dangling();

#ifdef TRIGGER_USE_AFTER_SCOPE
    // Reading through a pointer to a destroyed stack variable:
    std::printf("  g_dangling dereference (UAscope!): %.2f\n", *g_dangling);
    std::puts("  (ASan should have fired above this line)");
#else
    (void)g_dangling;
    std::puts("  pointer would dangle after fill_dangling() returns");
    std::puts("  compile with -DTRIGGER_USE_AFTER_SCOPE to see ASan report");
    // Fix: return the value, not a pointer to a local.
#endif
}

// ── Bug 3: Use-after-return ───────────────────────────────────────────
//
// Similar to use-after-scope, but the pointer escapes to the *caller*.
// ASan detects this only with:
//   ASAN_OPTIONS=detect_stack_use_after_return=1
// which causes ASan to heap-allocate stack frames and poison them on return.
//
// ASan report:
//   ERROR: AddressSanitizer: stack-use-after-return on address ...
//
[[nodiscard]] static double* make_dangling_return()
{
    double price = 102.50;
    return &price; // returns pointer to local — UB in standard C++
}

static void demo_use_after_return()
{
    section("use-after-return");
    double* p = make_dangling_return(); // p points to destroyed frame
    (void)p;
    std::puts("  pointer p = &local in make_dangling_return()");
    std::puts("  re-run with ASAN_OPTIONS=detect_stack_use_after_return=1");
    std::puts("  and dereference p to see the ASan report");
    // Fix: return by value, or allocate on heap.
}

// ── Bug 4: LeakSanitizer ──────────────────────────────────────────────
//
// LSan is part of ASan on Linux.  On macOS, enable with:
//   ASAN_OPTIONS=detect_leaks=1 ./asan_stack
// Standalone (Linux, no ASan): -fsanitize=leak
//
// LSan report:
//   Direct leak of 400 byte(s) in 1 object(s) allocated at:
//     ... demo_lsan ...
//
static void demo_lsan()
{
    section("LeakSanitizer");

    auto* ticks    = new double[50];   // 400 bytes — never freed
    auto* book     = new double[4]{};  // 32 bytes — freed below (not leaked)

    for (int i = 0; i < 50; ++i) ticks[i] = 100.0 + i * 0.01;
    std::printf("  allocated ticks[50] at %p (intentionally leaked)\n",
                static_cast<void*>(ticks));
    std::printf("  allocated book[4]   at %p (freed below — not leaked)\n",
                static_cast<void*>(book));

#ifndef TRIGGER_LEAK
    delete[] ticks; // fix: free it
#endif
    delete[] book;  // always freed

    std::puts("  compile with -DTRIGGER_LEAK and -fsanitize=address");
    std::puts("  then run with ASAN_OPTIONS=detect_leaks=1");
}

// ── Bug 5: Global buffer overflow ─────────────────────────────────────
//
// ASan adds redzones around global arrays too.
// Report:
//   ERROR: AddressSanitizer: global-buffer-overflow on address ...
//
static double g_rates[4] = {0.01, 0.02, 0.03, 0.04};

static void demo_global_overflow()
{
    section("global buffer overflow");

#ifdef TRIGGER_GLOBAL_OVF
    g_rates[4] = 0.05; // past the end of g_rates — global redzone
    std::puts("  (ASan should have fired above this line)");
#else
    std::printf("  g_rates[3]=%.2f (last valid element)\n", g_rates[3]);
    std::puts("  compile with -DTRIGGER_GLOBAL_OVF to see ASan report");
#endif
}

int main()
{
    std::puts("=== ASan stack + LSan ===");
    demo_stack_overflow();
    demo_use_after_scope();
    demo_use_after_return();
    demo_lsan();
    demo_global_overflow();
    std::puts("\n=== done ===");
}
