#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// clang-tidy — patterns caught by modernize-*, bugprone-*, performance-*.
//
// clang-tidy is a Clang-AST-based linter that checks for coding style
// issues, modernisation opportunities, and actual bugs.
//
// Compile this file (no external deps):
//   g++ -std=c++20 -O2 -Wall 07_clang_tidy.cpp -o tidy && ./tidy
//
// Run clang-tidy (needs compile_commands.json in build/):
//   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
//   clang-tidy -p build/ 07_clang_tidy.cpp \
//     -checks='modernize-*,bugprone-*,readability-*,performance-*'
//
// Auto-fix safe checks:
//   clang-tidy -p build/ -fix \
//     -checks='modernize-use-nullptr,modernize-use-override' \
//     07_clang_tidy.cpp
//
// Configure per-project via .clang-tidy at the repo root.
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── modernize-use-nullptr ─────────────────────────────────────────────
// Finds: NULL or 0 used as a null pointer.
// Fix:   replace with nullptr.
//
static void demo_nullptr()
{
    section("modernize-use-nullptr");

    // BAD: const char* p = NULL;
    // BAD: if (p == 0) { ... }
    // FIX:
    const char* p = nullptr;
    if (p == nullptr) {
        std::puts("  p is nullptr — using nullptr (not NULL or 0)");
    }
    // nullptr is type-safe: it can't be accidentally used as an integer.
    // nullptr has type std::nullptr_t — no implicit int conversion.
}

// ── modernize-use-override ────────────────────────────────────────────
// Finds: virtual function overrides without 'override' keyword.
// Fix:   add 'override' (and remove redundant 'virtual').
//
struct PriceFeed {
    virtual ~PriceFeed() = default;
    virtual double latest_price() const = 0;
    virtual void subscribe()            = 0;
};

struct MarketDataFeed : PriceFeed {
    // BAD: virtual double latest_price() const { return 101.0; }
    // FIX:
    double latest_price() const override { return 101.0; }
    void   subscribe()          override { std::puts("  subscribed"); }
};

static void demo_override()
{
    section("modernize-use-override");
    MarketDataFeed feed;
    std::printf("  price = %.2f\n", feed.latest_price());
    std::puts("  'override' makes compiler verify the function exists in base");
}

// ── modernize-use-auto ────────────────────────────────────────────────
// Finds: redundant type names when the type is obvious from context.
// Fix:   use 'auto'.
//
static void demo_auto()
{
    section("modernize-use-auto");

    // BAD: std::vector<double>::iterator it = v.begin();
    // FIX:
    std::vector<double> prices{100.0, 101.0, 102.0};
    auto it = prices.begin(); // type is obvious from context
    std::printf("  first price via auto iterator: %.1f\n", *it);

    // BAD: std::unique_ptr<MarketDataFeed> f = std::make_unique<MarketDataFeed>();
    // FIX:
    auto feed = std::make_unique<MarketDataFeed>();
    std::printf("  feed price: %.2f\n", feed->latest_price());
}

// ── modernize-use-range-for ───────────────────────────────────────────
// Finds: index-based loops that can use range-for.
// Fix:   use range-for.
//
static void demo_range_for()
{
    section("modernize-use-range-for");

    std::vector<double> ticks{100.1, 100.2, 100.3};

    // BAD:
    // for (std::size_t i = 0; i < ticks.size(); ++i)
    //     printf("%.1f\n", ticks[i]);
    //
    // FIX:
    double sum = 0.0;
    for (const double& t : ticks) sum += t;
    std::printf("  sum via range-for: %.1f\n", sum);
}

// ── bugprone-use-after-move ───────────────────────────────────────────
// Finds: using an object after std::move() has been called on it.
//
static void demo_use_after_move()
{
    section("bugprone-use-after-move");

    std::string name{"AAPL"};
    std::string target = std::move(name);
    // BAD: printf("%s\n", name.c_str());  // name is in a valid but unspecified state
    // FIX: don't use 'name' after move, or reassign it first:
    name = "MSFT"; // reassignment is safe
    std::printf("  target=%s  name (after re-assign)=%s\n",
                target.c_str(), name.c_str());
}

// ── bugprone-integer-division ─────────────────────────────────────────
// Finds: integer division result assigned to floating-point.
//
static void demo_int_division()
{
    section("bugprone-integer-division");

    int trades = 7;
    int days   = 2;

    // BAD: double avg = trades / days;   // integer division: 3.0, not 3.5
    // FIX:
    double avg = static_cast<double>(trades) / days;
    std::printf("  trades/days = %.1f (correct fp division)\n", avg);

    // The bug version (for illustration):
    double bad = trades / days; // int division: 3, assigned to double → 3.0
    std::printf("  bug version: %d / %d = %.1f (truncated!)\n", trades, days, bad);
}

// ── performance-unnecessary-copy-initialization ───────────────────────
// Finds: local variable initialised by copying when const ref suffices.
//
static void demo_unnecessary_copy()
{
    section("performance-unnecessary-copy-initialization");

    const std::vector<double> prices{100.0, 101.0, 102.0};

    // BAD: auto p = prices;   // copies the whole vector
    // FIX: use const ref when you only need to read:
    const auto& p = prices;   // reference — no copy
    std::printf("  p[0]=%.1f (const ref — no copy made)\n", p[0]);
}

// ── performance-inefficient-string-concatenation ──────────────────────
// Finds: string concatenation inside a loop using operator+.
//
static void demo_string_concat()
{
    section("performance-inefficient-string-concatenation");

    // BAD (quadratic): string log = ""; for (auto& e : events) log += e;
    // FIX: pre-allocate and append, or use std::ostringstream / fmt:
    std::string log;
    log.reserve(256);
    for (const char* sym : {"AAPL", "MSFT", "GOOG"}) {
        log += sym;
        log += ' ';
    }
    std::printf("  log = '%s'\n", log.c_str());
}

// ── readability-magic-numbers ─────────────────────────────────────────
// Finds: numeric literals embedded directly in expressions (magic numbers).
// Fix:   assign to a named constexpr constant.
//
static void demo_magic_numbers()
{
    section("readability-magic-numbers");

    // BAD: double fee = qty * price * 0.00025;
    // FIX:
    constexpr double kCommissionRate = 0.00025; // basis point * 2.5
    int qty = 100; double price = 101.5;
    double fee = qty * price * kCommissionRate;
    std::printf("  commission fee = %.4f  (using named constant)\n", fee);
}

// ── readability-const-return-type ─────────────────────────────────────
// Finds: 'const' return type on a non-reference return (useless).
//
// BAD: const int get_qty() { return 100; }   // const on value return is ignored
// FIX: int get_qty() { return 100; }
static int get_qty() { return 100; }     // correct — no pointless const
static void demo_const_return()
{
    section("readability-const-return-type");
    std::printf("  qty = %d  (non-const return type — const would be ignored)\n",
                get_qty());
}

int main()
{
    std::puts("=== clang-tidy patterns ===");
    demo_nullptr();
    demo_override();
    demo_auto();
    demo_range_for();
    demo_use_after_move();
    demo_int_division();
    demo_unnecessary_copy();
    demo_string_concat();
    demo_magic_numbers();
    demo_const_return();
    std::puts("\n=== run: clang-tidy -checks='modernize-*,bugprone-*' ... ===");
}
