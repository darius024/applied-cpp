#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Parameterized tests.
//
// TEST_P: run the same test body over many inputs without duplication.
//
//   1. Inherit from ::testing::TestWithParam<T>.
//   2. Call GetParam() inside TEST_P to get the current value.
//   3. Register inputs with INSTANTIATE_TEST_SUITE_P.
//
// Input generators:
//   Values(v1, v2, ...)       — explicit list
//   ValuesIn(container)       — from any iterable
//   Range(start, end, step)   — arithmetic range
//   Combine(g1, g2)           — Cartesian product of two generators
//
// TYPED_TEST: run the same test for a compile-time list of types.
//   Useful for generic numeric code (float / double / long double).
//
// Compile:  g++ -std=c++20 03_gtest_parameterized.cpp \
//               -lgtest_main -lgtest -pthread -o t3 && ./t3
// ─────────────────────────────────────────────────────────────────────

// ── Subject under test ────────────────────────────────────────────────

// Returns the annualised Black-Scholes call price.
double bs_call(double S, double K, double r, double sigma, double T)
{
    auto N   = [](double x){ return 0.5 * std::erfc(-x / std::sqrt(2.0)); };
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                / (sigma * std::sqrt(T));
    double d2 = d1 - sigma * std::sqrt(T);
    return S * N(d1) - K * std::exp(-r * T) * N(d2);
}

// Returns the portfolio return: sum of weight_i * return_i.
template <typename T>
T portfolio_return(const std::vector<T>& w, const std::vector<T>& r)
{
    return std::inner_product(w.begin(), w.end(), r.begin(), T{0});
}

// ═════════════════════════════════════════════════════════════════════
// Value-parameterized: clamp boundary cases
// ═════════════════════════════════════════════════════════════════════

struct ClampCase { int input, lo, hi, expected; };

class ClampParamTest : public ::testing::TestWithParam<ClampCase> {};

TEST_P(ClampParamTest, ClampsCorrectly)
{
    auto [input, lo, hi, expected] = GetParam();
    EXPECT_EQ(std::clamp(input, lo, hi), expected);
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryValues,
    ClampParamTest,
    ::testing::Values(
        ClampCase{  5, 0, 10,  5 },  // in range
        ClampCase{ -3, 0, 10,  0 },  // below lo
        ClampCase{ 15, 0, 10, 10 },  // above hi
        ClampCase{  0, 0, 10,  0 },  // exactly lo
        ClampCase{ 10, 0, 10, 10 }   // exactly hi
    )
);

// ═════════════════════════════════════════════════════════════════════
// Combine: Cartesian product — test a grid of (S, K) pairs
// ═════════════════════════════════════════════════════════════════════

class BSBoundsTest
    : public ::testing::TestWithParam<std::tuple<double, double>> {};

// A call price must be in [0, S] regardless of moneyness.
TEST_P(BSBoundsTest, PriceInValidRange)
{
    auto [S, K] = GetParam();
    double price = bs_call(S, K, 0.05, 0.20, 1.0);
    EXPECT_GE(price, 0.0);
    EXPECT_LE(price, S);
}

INSTANTIATE_TEST_SUITE_P(
    SpotStrikeGrid,
    BSBoundsTest,
    ::testing::Combine(
        ::testing::Values(80.0, 100.0, 120.0),  // spot prices
        ::testing::Values(90.0, 100.0, 110.0)   // strike prices
    )
);

// ═════════════════════════════════════════════════════════════════════
// ValuesIn: read inputs from a container at runtime
// ═════════════════════════════════════════════════════════════════════

// In real projects, ValuesIn is used to load test cases from CSV/JSON.
static const std::vector<double> sigma_values = {0.10, 0.15, 0.20, 0.30, 0.40};

class BSVolTest : public ::testing::TestWithParam<double> {};

// Higher vol → higher call price (all else equal).
TEST_P(BSVolTest, HigherVolGivesHigherPrice)
{
    double sigma = GetParam();
    double price_lo = bs_call(100.0, 100.0, 0.05, sigma,       1.0);
    double price_hi = bs_call(100.0, 100.0, 0.05, sigma + 0.05, 1.0);
    EXPECT_LT(price_lo, price_hi);
}

INSTANTIATE_TEST_SUITE_P(VolSurface, BSVolTest, ::testing::ValuesIn(sigma_values));

// ═════════════════════════════════════════════════════════════════════
// Typed tests: same logic for float, double, long double
// ═════════════════════════════════════════════════════════════════════

template <typename T>
class PortfolioReturnTest : public ::testing::Test {};

using FloatingTypes = ::testing::Types<float, double, long double>;
TYPED_TEST_SUITE(PortfolioReturnTest, FloatingTypes);

TYPED_TEST(PortfolioReturnTest, WeightedSumIsCorrect)
{
    // TypeParam is the current type (float, double, or long double).
    std::vector<TypeParam> w = {TypeParam{0.6}, TypeParam{0.4}};
    std::vector<TypeParam> r = {TypeParam{0.10}, TypeParam{0.05}};
    TypeParam result = portfolio_return(w, r);
    // 0.6 * 0.10 + 0.4 * 0.05 = 0.08
    EXPECT_NEAR(static_cast<double>(result), 0.08, 1e-5);
}

TYPED_TEST(PortfolioReturnTest, EmptyPortfolioReturnsZero)
{
    std::vector<TypeParam> w, r;
    EXPECT_NEAR(static_cast<double>(portfolio_return(w, r)), 0.0, 1e-12);
}
