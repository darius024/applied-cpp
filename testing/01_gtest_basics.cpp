#include <gtest/gtest.h>
#include <cmath>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// Google Test basics.
//
// TEST(SuiteName, TestName) — free-standing test; no shared state.
//
// EXPECT_* vs ASSERT_*:
//   EXPECT_* — records failure, continues the test.   Use by default.
//   ASSERT_* — records failure, aborts the test.      Use when continuing
//              would cause a null dereference or corrupt test output.
//
// Floating point: NEVER use EXPECT_EQ with doubles.
//   Use EXPECT_NEAR(a, b, eps) or EXPECT_DOUBLE_EQ(a, b).
//
// Install:  brew install googletest
// Compile:  g++ -std=c++20 01_gtest_basics.cpp \
//               -lgtest_main -lgtest -pthread -o t1 && ./t1
// ─────────────────────────────────────────────────────────────────────

// ── Subjects under test ───────────────────────────────────────────────

// Clamp an integer to [lo, hi].
int clamp(int v, int lo, int hi)
{
    if (lo > hi) throw std::invalid_argument("lo > hi");
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Simplified Black-Scholes call price (closed-form).
struct BSParams { double S, K, r, sigma, T; };

double bs_call(BSParams p)
{
    if (p.sigma <= 0.0 || p.T <= 0.0)
        throw std::invalid_argument("sigma and T must be positive");
    auto N   = [](double x){ return 0.5 * std::erfc(-x / std::sqrt(2.0)); };
    double d1 = (std::log(p.S / p.K) + (p.r + 0.5 * p.sigma * p.sigma) * p.T)
                / (p.sigma * std::sqrt(p.T));
    double d2 = d1 - p.sigma * std::sqrt(p.T);
    return p.S * N(d1) - p.K * std::exp(-p.r * p.T) * N(d2);
}

// ═════════════════════════════════════════════════════════════════════
// Integer tests
// ═════════════════════════════════════════════════════════════════════

// ── Equality and ordering ─────────────────────────────────────────────
TEST(ClampTest, ReturnsValueWhenInRange)
{
    EXPECT_EQ(clamp(5, 0, 10), 5);
}

TEST(ClampTest, ClampsToLower)
{
    EXPECT_EQ(clamp(-3, 0, 10), 0);
    EXPECT_LE(clamp(-3, 0, 10), 0); // same fact; different macro
}

TEST(ClampTest, ClampsToUpper)
{
    EXPECT_EQ(clamp(15, 0, 10), 10);
    EXPECT_GE(clamp(15, 0, 10), 10);
}

TEST(ClampTest, EdgeCasesExactlyAtBoundary)
{
    EXPECT_EQ(clamp(0,  0, 10),  0); // exactly lo
    EXPECT_EQ(clamp(10, 0, 10), 10); // exactly hi
}

// ── Exception tests ───────────────────────────────────────────────────
TEST(ClampTest, ThrowsOnInvertedBounds)
{
    EXPECT_THROW(clamp(5, 10, 0), std::invalid_argument);
}

TEST(ClampTest, DoesNotThrowOnValidBounds)
{
    EXPECT_NO_THROW(clamp(5, 0, 10));
}

// ── ASSERT aborts the current test on first failure ───────────────────
// Use ASSERT when continuing past a failure would crash or mislead.
TEST(ClampTest, AssertBeforeDereference)
{
    ASSERT_NO_THROW(clamp(5, 0, 10)); // if this threw, the next line would fail
    EXPECT_EQ(clamp(5, 0, 10), 5);   // safe to reach
}

// ═════════════════════════════════════════════════════════════════════
// Floating-point tests — always use EXPECT_NEAR / EXPECT_DOUBLE_EQ
// ═════════════════════════════════════════════════════════════════════

TEST(BSCallTest, KnownATMValue)
{
    // ATM call: S=K=100, r=5%, σ=20%, T=1y — textbook result ≈ 10.45.
    BSParams p{100.0, 100.0, 0.05, 0.20, 1.0};
    EXPECT_NEAR(bs_call(p), 10.45, 0.01);
}

TEST(BSCallTest, DeepInTheMoneyApproachesIntrinsic)
{
    // S >> K, zero rate: call price ≈ S − K.
    BSParams p{200.0, 100.0, 0.0, 0.01, 1.0};
    EXPECT_NEAR(bs_call(p), 100.0, 1.0);
}

TEST(BSCallTest, CallPriceIsAlwaysNonNegative)
{
    BSParams p{100.0, 100.0, 0.05, 0.20, 1.0};
    EXPECT_TRUE(bs_call(p) >= 0.0);
    EXPECT_FALSE(bs_call(p) < 0.0);
}

TEST(BSCallTest, ThrowsOnNonPositiveSigma)
{
    BSParams p{100.0, 100.0, 0.05, 0.0, 1.0}; // sigma = 0
    EXPECT_THROW(bs_call(p), std::invalid_argument);
}

TEST(BSCallTest, ThrowsOnNonPositiveMaturity)
{
    BSParams p{100.0, 100.0, 0.05, 0.20, 0.0}; // T = 0
    EXPECT_THROW(bs_call(p), std::invalid_argument);
}

TEST(BSCallTest, DoesNotThrowOnValidParams)
{
    EXPECT_NO_THROW(bs_call({100.0, 100.0, 0.05, 0.20, 1.0}));
}

// ── EXPECT_ANY_THROW: when the exact type does not matter ─────────────
// Prefer EXPECT_THROW with a type; this variant is a last resort.
TEST(BSCallTest, AnyThrowOnZeroSigma)
{
    EXPECT_ANY_THROW(bs_call({100.0, 100.0, 0.05, 0.0, 1.0}));
}
