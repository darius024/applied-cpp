#include <gtest/gtest.h>
#include <cassert>
#include <cstdlib>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// Death tests: verify that code terminates or throws as expected.
//
// EXPECT_DEATH(statement, stderr_regex)
//   Forks a child process, runs statement, expects it to terminate via
//   a signal (SIGABRT from assert, std::abort, etc.) and that stderr
//   output matches the regex. Use "" to match any output.
//
// EXPECT_EXIT(statement, predicate, stderr_regex)
//   Like EXPECT_DEATH but also checks the exit code:
//     ::testing::ExitedWithCode(0)   — exited cleanly
//     ::testing::KilledBySignal(sig) — killed by signal
//
// EXPECT_THROW(statement, ExceptionType)
//   Verifies a throw; runs in the same process (no fork needed).
//
// Convention: name suites ending in "DeathTest" — gtest runs all death
//   tests BEFORE regular tests to prevent process-level side effects.
//
// Important: compile WITHOUT -DNDEBUG so assert() is active.
//
// Compile:  g++ -std=c++20 04_gtest_death_tests.cpp \
//               -lgtest_main -lgtest -pthread -o t4 && ./t4
// ─────────────────────────────────────────────────────────────────────

// ── Subjects under test ───────────────────────────────────────────────

double safe_divide(double a, double b)
{
    if (b == 0.0) throw std::domain_error("division by zero");
    return a / b;
}

// Contract enforced with assert — violating it calls abort (SIGABRT).
int positive_sqrt(int n)
{
    assert(n >= 0 && "input must be non-negative");
    return static_cast<int>(std::sqrt(static_cast<double>(n)));
}

// Encodes a domain invariant that should never fire in correct code.
void require_positive_price(double price)
{
    if (price <= 0.0) {
        std::fputs("FATAL: price must be positive\n", stderr);
        std::abort();
    }
}

// ═════════════════════════════════════════════════════════════════════
// Exception tests (same-process; no fork)
// ═════════════════════════════════════════════════════════════════════

TEST(SafeDivideTest, ThrowsOnZeroDenominator)
{
    EXPECT_THROW(safe_divide(1.0, 0.0), std::domain_error);
}

TEST(SafeDivideTest, DoesNotThrowOnValidInput)
{
    EXPECT_NO_THROW(safe_divide(10.0, 2.0));
}

TEST(SafeDivideTest, ResultIsCorrect)
{
    EXPECT_NEAR(safe_divide(10.0, 4.0), 2.5, 1e-9);
}

TEST(SafeDivideTest, AnyThrowOnZeroDenominator)
{
    // Prefer EXPECT_THROW with a type; this variant is a last resort.
    EXPECT_ANY_THROW(safe_divide(5.0, 0.0));
}

// ═════════════════════════════════════════════════════════════════════
// Death tests — suite names end in "DeathTest" by convention
// ═════════════════════════════════════════════════════════════════════

TEST(PositiveSqrtDeathTest, AbortsOnNegativeInput)
{
    // The child process triggers assert(n >= 0), which calls abort().
    // We pass "" as the regex — match any stderr output.
    EXPECT_DEATH(positive_sqrt(-1), "");
}

TEST(PositiveSqrtDeathTest, DoesNotAbortOnZero)
{
    // Calling the function normally — the process won't be forked.
    // If positive_sqrt(0) didn't crash, the test passes.
    EXPECT_EQ(positive_sqrt(0), 0);
}

TEST(PriceValidationDeathTest, AbortsOnNonPositivePrice)
{
    // Match against the text written to stderr by the abort handler.
    EXPECT_DEATH(require_positive_price(0.0),  "FATAL");
    EXPECT_DEATH(require_positive_price(-1.0), "FATAL");
}

TEST(PriceValidationDeathTest, DoesNotAbortOnValidPrice)
{
    require_positive_price(100.0); // no abort — test passes silently
}

// ── EXPECT_EXIT: also check the exit code ─────────────────────────────
TEST(ExitDeathTest, ExitsCleanlyWithCode0)
{
    EXPECT_EXIT(std::exit(0), ::testing::ExitedWithCode(0), "");
}
