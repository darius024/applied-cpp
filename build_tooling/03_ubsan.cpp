#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// ─────────────────────────────────────────────────────────────────────
// UndefinedBehaviorSanitizer (UBSan).
//
// UBSan instruments specific operations and inserts a runtime check
// before each.  When a check fails it prints a diagnostic and either
// continues (halt_on_error=0) or aborts (default with -fsanitize=undefined).
//
// Unlike ASan, UBSan has negligible memory overhead (no shadow map).
// Typical runtime overhead: 5–15%.
//
// Compile (safe, no sanitizer):
//   g++ -std=c++20 -O1 -g 03_ubsan.cpp -o ubsan && ./ubsan
//
// Compile with UBSan:
//   g++ -std=c++20 -O1 -g -fsanitize=undefined \
//       -fno-omit-frame-pointer \
//       03_ubsan.cpp -o ubsan && ./ubsan
//
// See all reports without aborting:
//   UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1 ./ubsan
//
// Fine-grained subsets (add to -fsanitize=):
//   signed-integer-overflow  null  alignment  shift-exponent  bounds
//   float-divide-by-zero     enum  function   vptr
//
// Combine with ASan:
//   -fsanitize=address,undefined
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── Bug 1: Signed integer overflow ────────────────────────────────────
//
// Signed overflow is *undefined behaviour* in C++.
// The compiler may assume it never happens and optimise accordingly
// (e.g., `for (int i = 0; i >= 0; ++i)` may become an infinite loop).
//
// UBSan report:
//   runtime error: signed integer overflow: 2147483647 + 1 cannot be
//   represented in type 'int'
//
static void demo_signed_overflow()
{
    section("signed integer overflow");
    int a = INT_MAX;
    std::printf("  INT_MAX = %d\n", a);

    // Without UBSan: wraps to INT_MIN on most platforms (implementation-defined).
    // With    UBSan: runtime error reported.
    volatile int b = a + 1; // overflow — UB
    std::printf("  INT_MAX + 1 = %d  (UB — UBSan flags this)\n", b);
    std::puts("  Fix: use unsigned arithmetic, or __builtin_add_overflow()");

    // Correct overflow-safe addition:
    int result;
    if (__builtin_add_overflow(a, 1, &result))
        std::puts("  __builtin_add_overflow correctly detected overflow");
}

// ── Bug 2: Null pointer dereference ───────────────────────────────────
//
// UBSan report:
//   runtime error: null pointer passed as argument 1, which is declared to
//   never be null
//   OR: load of null pointer of type 'double'
//
static double* maybe_find_price(bool found)
{
    if (found) {
        static double price = 100.0;
        return &price;
    }
    return nullptr; // caller must check!
}

static void demo_null_deref()
{
    section("null pointer dereference");
    double* p = maybe_find_price(false); // intentionally returns nullptr

    std::printf("  p = %p\n", static_cast<void*>(p));

    if (p) {
        std::printf("  price = %.2f\n", *p);
    } else {
        std::puts("  price not found — null pointer not dereferenced (safe)");
    }

    // Simulate the bug: uncomment to see UBSan fire.
    // double bad = *p;  // null deref — UBSan: load of null pointer
    std::puts("  to trigger: remove the null check and dereference p");
}

// ── Bug 3: Misaligned memory access ───────────────────────────────────
//
// Many CPUs require T* to be aligned to alignof(T).
// Violating this is UB; on x86 it's often tolerated (but slow);
// on ARM/RISC-V it typically causes a SIGBUS.
//
// UBSan report:
//   runtime error: load of misaligned address ... for type 'double',
//   which requires 8 byte alignment
//
static void demo_misaligned()
{
    section("misaligned memory access");

    alignas(8) char buf[16]{};
    // buf is aligned to 8 bytes.  A double* cast starting at offset 1
    // gives alignment of 1 — violates double's required 8-byte alignment.
    char* raw = buf + 1; // intentionally misaligned

    // Correct: write as bytes, read as bytes.
    double value = 42.5;
    std::memcpy(raw, &value, sizeof(double)); // always safe

    double loaded;
    std::memcpy(&loaded, raw, sizeof(double));
    std::printf("  memcpy round-trip: %.1f (safe — no alignment UB)\n", loaded);

    // The UB version:
    // double* p = reinterpret_cast<double*>(raw); // misaligned pointer
    // double v = *p;  // UBSan: misaligned load
    std::puts("  reinterpret_cast<double*>(misaligned_ptr) is UB — use memcpy");
}

// ── Bug 4: Shift amount out of range ──────────────────────────────────
//
// Shifting by ≥ width or a negative amount is UB.
// Shifting a signed value into/past the sign bit is UB (before C++20
// for left shift; C++20 makes left-shift on signed well-defined).
//
// UBSan report:
//   runtime error: shift exponent 32 is too large for 32-bit type 'int'
//
static void demo_shift_ub()
{
    section("shift out of range");
    unsigned int flags = 0x01u;
    std::printf("  flags = 0x%X\n", flags);

    // Safe: shift by [0, 31]
    for (int s : {0, 7, 15, 31}) {
        unsigned int shifted = flags << s;
        std::printf("  flags << %2d = 0x%08X\n", s, shifted);
    }

    // UB: shift by 32 (= width of unsigned int on most platforms)
    volatile int bad_shift = 32;
    volatile unsigned int bad = flags << bad_shift; // UBSan fires
    (void)bad;
    std::puts("  flags << 32 is UB (width == 32) — UBSan flags this");

    // Fix: guard the shift:
    int n = 32;
    if (n < static_cast<int>(sizeof(unsigned int) * 8))
        std::printf("  safe shift guard: %d < 32, would shift\n", n);
    else
        std::puts("  safe shift guard: shift amount >= width, skipped");
}

// ── Bug 5: Invalid enum cast ──────────────────────────────────────────
//
// Casting an integer to an enum that has no corresponding enumerator
// is UB (for scoped enums, the underlying value is valid; for unscoped
// enums it's implementation-defined beyond the range).
//
// UBSan report:
//   runtime error: load of value 99, which is not a valid value for
//   type 'Side'
//
enum class Side : int { Buy = 0, Sell = 1 };

static void demo_enum_cast()
{
    section("invalid enum cast");
    int raw_side = 99; // not a valid Side
    auto side = static_cast<Side>(raw_side); // UBSan fires
    // Don't use side.value — its representation is UB.
    (void)side;
    std::puts("  static_cast<Side>(99) — UBSan: not a valid value for 'Side'");
    std::puts("  Fix: validate raw_side before casting");
}

// ── Bug 6: Integer division by zero ───────────────────────────────────
//
// UBSan report:
//   runtime error: division by zero
//
static void demo_div_zero()
{
    section("integer division by zero");
    int numerator = 100;
    volatile int denominator = 0; // volatile prevents compile-time elim

    // Without UBSan: typically raises SIGFPE on x86.
    // With    UBSan: runtime error reported, then continues or aborts.
    int result = numerator / denominator; // UBSan: division by zero
    std::printf("  100 / 0 = %d  (UB — UBSan flags this)\n", result);
    std::puts("  Fix: guard with `if (denominator == 0) return;` before dividing");
}

// ── Bug 7: VLA / array out-of-bounds ──────────────────────────────────
//
// -fsanitize=bounds catches constant-index OOB (when the array size is
// known at compile time).
//
// UBSan report:
//   runtime error: index 5 out of bounds for type 'int [5]'
//
static void demo_bounds()
{
    section("array out of bounds (-fsanitize=bounds)");
    int arr[5] = {1, 2, 3, 4, 5};
    volatile int idx = 5; // runtime value prevents compile-time warning

    if (idx >= 0 && idx < 5) {
        std::printf("  arr[%d] = %d (safe)\n", static_cast<int>(idx), arr[idx]);
    } else {
        std::printf("  idx=%d is out of [0,5) — skipped (safe path)\n",
                    static_cast<int>(idx));
        // To trigger: remove the bounds check: int v = arr[idx];
        std::puts("  remove the bounds check to see -fsanitize=bounds fire");
    }
}

int main()
{
    std::puts("=== UndefinedBehaviorSanitizer ===");
    demo_signed_overflow();
    demo_null_deref();
    demo_misaligned();
    demo_shift_ub();
    demo_enum_cast();
    demo_div_zero();
    demo_bounds();
    std::puts("\n=== compile with -fsanitize=undefined to see all reports ===");
}
