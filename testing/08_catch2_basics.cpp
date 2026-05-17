// ─────────────────────────────────────────────────────────────────────
// Catch2 v3 basics.
//
// TEST_CASE("description", "[tag1][tag2]")
//   — top-level test. Tags let you run subsets: ./test "[portfolio]"
//
// SECTION("description")
//   — nested scope. Each SECTION re-runs the code above it from scratch,
//     giving clean shared setup without a fixture class.
//
// REQUIRE vs CHECK:
//   REQUIRE — aborts the test on failure  (like ASSERT_* in gtest)
//   CHECK   — records failure, continues  (like EXPECT_* in gtest)
//
// Floating-point: use Approx — never REQUIRE(a == b) for doubles.
//   Approx(v)                 — relative tolerance: ~1 ULP by default
//   Approx(v).epsilon(1e-9)   — relative tolerance ε
//   Approx(v).margin(1e-12)   — absolute tolerance
//
// Exceptions:
//   REQUIRE_THROWS_AS(expr, T)          — throws T or subtype
//   REQUIRE_THROWS_WITH(expr, "msg")    — message matches string/regex
//   REQUIRE_NOTHROW(expr)
//   CHECK_THROWS_AS / CHECK_NOTHROW    — non-aborting variants
//
// Install:  brew install catch2
// Compile:  g++ -std=c++20 08_catch2_basics.cpp \
//               -lCatch2Main -lCatch2 -o t8 && ./t8
// ─────────────────────────────────────────────────────────────────────

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

using Catch::Approx;

// ── Subjects under test ───────────────────────────────────────────────

double portfolio_return(const std::vector<double>& weights,
                        const std::vector<double>& returns)
{
    if (weights.size() != returns.size())
        throw std::invalid_argument("size mismatch");
    double r = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) r += weights[i] * returns[i];
    return r;
}

double portfolio_variance(const std::vector<double>& w,
                          const std::vector<std::vector<double>>& cov)
{
    int n = static_cast<int>(w.size());
    double var = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            var += w[i] * w[j] * cov[i][j];
    return var;
}

double sharpe_ratio(double ret, double risk_free, double vol)
{
    if (vol <= 0.0) throw std::domain_error("vol must be positive");
    return (ret - risk_free) / vol;
}

// ── Simple tests ──────────────────────────────────────────────────────

TEST_CASE("portfolio_return computes weighted sum", "[portfolio]")
{
    std::vector<double> w = {0.6, 0.4};
    std::vector<double> r = {0.10, 0.05};
    // 0.6 * 0.10 + 0.4 * 0.05 = 0.08
    REQUIRE(portfolio_return(w, r) == Approx(0.08).epsilon(1e-9));
}

TEST_CASE("portfolio_return throws on size mismatch", "[portfolio]")
{
    REQUIRE_THROWS_AS(
        portfolio_return({0.5, 0.5}, {0.1}),
        std::invalid_argument
    );
}

TEST_CASE("sharpe_ratio calculation", "[portfolio]")
{
    // (0.12 - 0.02) / 0.15 ≈ 0.6667
    REQUIRE(sharpe_ratio(0.12, 0.02, 0.15) == Approx(0.6667).epsilon(1e-4));
}

TEST_CASE("sharpe_ratio throws on zero volatility", "[portfolio]")
{
    REQUIRE_THROWS_AS(sharpe_ratio(0.12, 0.02, 0.0), std::domain_error);
    REQUIRE_NOTHROW(sharpe_ratio(0.12, 0.02, 0.15));
}

// ── SECTION: each section re-runs the shared setup above it ──────────
// Equivalent to a gtest fixture but written as inline code.
// Here 'weights' is initialised once; each SECTION varies the covariance.

TEST_CASE("portfolio_variance under different correlation regimes", "[portfolio]")
{
    std::vector<double> w = {0.5, 0.5};
    double s1 = 0.2, s2 = 0.3;

    SECTION("uncorrelated assets")
    {
        // var = w1² σ1² + w2² σ2²
        std::vector<std::vector<double>> cov = {{s1*s1, 0.0}, {0.0, s2*s2}};
        double expected = 0.25 * s1*s1 + 0.25 * s2*s2; // 0.0325
        REQUIRE(portfolio_variance(w, cov) == Approx(expected).epsilon(1e-9));
    }

    SECTION("perfectly positively correlated")
    {
        // rho = 1: cov[i][j] = sigma_i * sigma_j
        std::vector<std::vector<double>> cov = {{s1*s1, s1*s2}, {s1*s2, s2*s2}};
        double expected = (0.5*s1 + 0.5*s2) * (0.5*s1 + 0.5*s2);
        REQUIRE(portfolio_variance(w, cov) == Approx(expected).epsilon(1e-9));
    }

    SECTION("perfectly negatively correlated — diversification eliminates variance")
    {
        // Equal vols, rho = -1: 50/50 portfolio has zero variance.
        double s = 0.2;
        std::vector<std::vector<double>> cov = {{s*s, -s*s}, {-s*s, s*s}};
        REQUIRE(portfolio_variance({0.5, 0.5}, cov) == Approx(0.0).margin(1e-12));
    }
}

// ── Nested SECTIONs: build scenarios step by step ─────────────────────
// Inner sections share the setup of all their enclosing sections.

TEST_CASE("Approx tolerance modes", "[approx]")
{
    SECTION("relative epsilon")
    {
        // epsilon(e): pass if |actual/expected - 1| ≤ e
        REQUIRE(1.001 == Approx(1.0).epsilon(0.01)); // 0.1% tolerance

        SECTION("tight epsilon rejects larger deviation")
        {
            CHECK_FALSE(1.02 == Approx(1.0).epsilon(0.001));
        }
    }

    SECTION("absolute margin")
    {
        // margin(m): pass if |actual - expected| ≤ m
        REQUIRE(0.000001 == Approx(0.0).margin(1e-5));
    }
}
