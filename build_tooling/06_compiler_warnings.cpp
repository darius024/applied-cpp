#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Compiler warnings — -Wall, -Wextra, -Wpedantic and friends.
//
// Warnings are the compiler's static analysis.  Many real bugs are
// caught for free with no runtime overhead.  Best practice:
//   1. Enable -Wall -Wextra -Wshadow -Wconversion in all builds.
//   2. Use -Werror in CI to prevent new warnings accumulating.
//   3. Suppress individual false positives with pragmas, never blanket.
//
// This file is intentionally written with suppressed warnings so it
// compiles cleanly, but each section shows the *pattern* that would
// be flagged and the FIXED form.
//
// Compile (clean — all patterns fixed):
//   g++ -std=c++20 -Wall -Wextra -Wshadow -Wconversion \
//       -Wsign-conversion -Wnull-dereference -Wold-style-cast \
//       -Wformat=2 -Wimplicit-fallthrough \
//       06_compiler_warnings.cpp -o warnings && ./warnings
//
// Compile with Werror to treat all warnings as errors:
//   g++ -std=c++20 -Wall -Wextra -Werror 06_compiler_warnings.cpp -o w && ./w
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── W1: -Wunused-variable / -Wunused-parameter ────────────────────────
// Warning:  unused variable 'unused_price'
// Warning:  unused parameter 'ctx'
//
// BAD (would warn):
//   double unused_price = 100.0;          // declared, never used
//   void process(int ctx, double price) { return; }  // ctx unused
//
// FIX: use [[maybe_unused]] or cast to void for intentionally unused names.
//
static void demo_unused()
{
    section("-Wunused-variable / -Wunused-parameter");

    [[maybe_unused]] double unused_price = 100.0;  // suppresses warning
    std::puts("  [[maybe_unused]] suppresses 'unused variable' warning");
    std::puts("  (void)param; is the pre-C++17 idiom for unused params");
}

// ── W2: -Wshadow ──────────────────────────────────────────────────────
// Warning:  declaration of 'price' shadows a previous local
//
// BAD:
//   double price = 100.0;
//   {
//     double price = 200.0;   // shadows outer price — which do you mean?
//     printf("%.1f\n", price);
//   }
//
// FIX: use distinct names.
//
static void demo_shadow()
{
    section("-Wshadow");

    double bid_price = 100.0;
    double ask_price = 101.0; // different name — no shadow
    {
        double spread = ask_price - bid_price; // clearly refers to outer
        std::printf("  bid=%.1f ask=%.1f spread=%.1f\n",
                    bid_price, ask_price, spread);
    }
    std::puts("  BAD: inner 'price' shadows outer 'price' — use distinct names");
}

// ── W3: -Wconversion / -Wsign-conversion ──────────────────────────────
// Warning:  implicit conversion from 'double' to 'int' loses precision
// Warning:  implicit conversion changes signedness
//
// BAD:
//   int qty = 3.7;          // implicit truncation — likely a bug
//   unsigned n = -1;        // wraps to UINT_MAX — almost always a bug
//   int diff = size_t_val;  // sign conversion
//
// FIX: explicit casts, or fix the type.
//
static void demo_conversion()
{
    section("-Wconversion / -Wsign-conversion");

    double price = 101.75;

    // BAD: int qty = price;  — implicit truncation
    int qty = static_cast<int>(price); // explicit cast — intention is clear
    std::printf("  price=%.2f truncated to qty=%d  (explicit cast)\n",
                price, qty);

    std::vector<int> v{1, 2, 3};
    // BAD: int n = v.size();  — sign-conversion (size_t to int)
    int n = static_cast<int>(v.size());
    std::printf("  v.size()=%zu, n=%d  (explicit sign-cast)\n", v.size(), n);
}

// ── W4: -Wold-style-cast ──────────────────────────────────────────────
// Warning:  use of old-style cast
//
// C-style casts are dangerous: they silently do const_cast, reinterpret_cast,
// or static_cast depending on context.  Use named casts instead.
//
// BAD:  int* p = (int*)malloc(n);
//       const char* s = (const char*)ptr;
//
// FIX:  use static_cast, reinterpret_cast, const_cast explicitly.
//
static void demo_old_style_cast()
{
    section("-Wold-style-cast");

    void* raw = nullptr;
    // BAD: int* p = (int*)raw;
    int* p = static_cast<int*>(raw); // intent is clear
    (void)p;
    std::puts("  static_cast<int*>(raw) is explicit; (int*)raw hides intent");

    const char* msg = "hello";
    // BAD: char* mutable_msg = (char*)msg;   — quietly casts away const
    // FIX: use const_cast — makes the removal of const visible in code review:
    // char* mutable_msg = const_cast<char*>(msg); // (and avoid doing this)
    (void)msg;
    std::puts("  const_cast is explicit; C-cast silently drops const");
}

// ── W5: -Wformat=2 ────────────────────────────────────────────────────
// Warning:  format string is not a string literal; format not checked
// Warning:  unknown conversion type character
//
// BAD: printf(user_input);           // format injection vulnerability
//      printf("%lf", some_int);      // wrong specifier for int
//
// FIX: always use a string literal as the format, match specifiers to types.
//
static void demo_format()
{
    section("-Wformat=2");

    const char* user_input = "some text from network";
    // BAD: printf(user_input);    — format injection if user_input has %s etc.
    std::printf("  safe: %s\n", user_input); // literal format string

    double price = 101.5;
    std::printf("  price: %.2f\n", price); // %f matches double — correct
    // BAD: printf("%d\n", price);  — %d doesn't match double (UB)
    std::puts("  -Wformat=2 catches format/type mismatches and non-literals");
}

// ── W6: -Wimplicit-fallthrough ────────────────────────────────────────
// Warning:  unannotated fall-through between switch labels
//
// Accidental fallthrough is a common bug.  Annotate intentional ones.
//
static void demo_fallthrough(int side)
{
    section("-Wimplicit-fallthrough");

    switch (side) {
        case 0:
            std::puts("  Buy order");
            break;
        case 1:
            std::puts("  Sell order");
            break;
        case 2:
            std::puts("  Short order (falls through to Sell handling)");
            [[fallthrough]]; // C++17: explicit annotation — no warning
        case 3:
            std::puts("  Short/Sell common handling");
            break;
        default:
            std::puts("  Unknown side");
    }
}

// ── W7: -Wnull-dereference ────────────────────────────────────────────
// Warning:  potential null pointer dereference
//
// The compiler can sometimes detect paths where a null deref may occur.
// This is different from ASan/UBSan — pure static analysis.
//
[[nodiscard]] static double* find_price(int id)
{
    if (id < 0) return nullptr;
    static double price = 102.0;
    return &price;
}

static void demo_null_dereference()
{
    section("-Wnull-dereference");

    double* p = find_price(1);
    if (p) {
        std::printf("  price = %.2f (null-checked before deref)\n", *p);
    } else {
        std::puts("  price not found — null deref avoided");
    }
    std::puts("  BAD: deref p without null check — -Wnull-dereference warns");
}

// ── W8: -Wundef ───────────────────────────────────────────────────────
// Warning:  'MY_MACRO' is not defined, evaluates to 0
//
// BAD: #if MY_MACRO   — if MY_MACRO is not #defined, this silently = 0
// FIX: #if defined(MY_MACRO) — explicit existence check
//
static void demo_undef()
{
    section("-Wundef");
#if defined(MY_MACRO)
    std::puts("  MY_MACRO is defined");
#else
    std::puts("  MY_MACRO is not defined — used defined() to check safely");
#endif
    std::puts("  BAD: #if MY_MACRO  (silently 0 if not defined)");
    std::puts("  FIX: #if defined(MY_MACRO)");
}

int main()
{
    std::puts("=== Compiler warnings demo ===");
    demo_unused();
    demo_shadow();
    demo_conversion();
    demo_old_style_cast();
    demo_format();
    demo_fallthrough(2);
    demo_null_dereference();
    demo_undef();
    std::puts("\n=== all patterns use the corrected (warning-free) form ===");
}
