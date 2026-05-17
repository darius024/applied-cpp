// ─────────────────────────────────────────────────────────────────────
// Catch2 BDD style.
//
// BDD (Behaviour-Driven Development) expresses tests as plain-language
// stories. The macros are aliases for TEST_CASE / SECTION:
//
//   SCENARIO  →  TEST_CASE
//   GIVEN     →  SECTION
//   WHEN      →  SECTION (nested inside GIVEN)
//   THEN      →  SECTION (nested inside WHEN)
//   AND_GIVEN / AND_WHEN / AND_THEN — chain additional conditions
//
// The semantics are identical to nested SECTIONs: each WHEN is entered
// fresh for every THEN, and each GIVEN is entered fresh for every WHEN.
//
// BDD is valuable when tests double as specifications that quant analysts
// or product owners will read — the Given/When/Then vocabulary removes
// the need to understand C++ to follow the logic.
//
// Install:  brew install catch2
// Compile:  g++ -std=c++20 09_catch2_bdd.cpp \
//               -lCatch2Main -lCatch2 -o t9 && ./t9
// ─────────────────────────────────────────────────────────────────────

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <optional>
#include <stdexcept>
#include <string>

using Catch::Approx;

// ── Domain model ──────────────────────────────────────────────────────

enum class Side { Buy, Sell };

struct Order {
    int         id;
    std::string symbol;
    Side        side;
    double      price;
    int         qty;
};

class RiskEngine
{
public:
    explicit RiskEngine(double max_notional) : max_notional_(max_notional) {}

    // Returns the rejection reason, or std::nullopt if approved.
    std::optional<std::string> check(const Order& o) const
    {
        if (o.qty   <= 0)   return "qty must be positive";
        if (o.price <= 0.0) return "price must be positive";
        if (o.price * o.qty > max_notional_) return "notional exceeds limit";
        return std::nullopt;
    }

    double max_notional() const { return max_notional_; }

private:
    double max_notional_;
};

// ═════════════════════════════════════════════════════════════════════
// BDD scenarios
// ═════════════════════════════════════════════════════════════════════

SCENARIO("risk engine approves and rejects orders based on notional", "[risk]")
{
    GIVEN("a risk engine with a 1,000,000 notional limit")
    {
        RiskEngine risk{1'000'000.0};

        WHEN("an order with notional well below the limit is submitted")
        {
            Order o{1, "AAPL", Side::Buy, 150.0, 100}; // 15,000 notional

            THEN("it is approved")
            {
                REQUIRE_FALSE(risk.check(o).has_value());
            }
        }

        WHEN("an order with notional above the limit is submitted")
        {
            Order o{2, "AAPL", Side::Buy, 150.0, 10'000}; // 1,500,000 notional

            THEN("it is rejected")
            {
                auto result = risk.check(o);
                REQUIRE(result.has_value());
            }

            AND_THEN("the rejection message mentions notional")
            {
                REQUIRE(risk.check(o).value() == "notional exceeds limit");
            }
        }

        WHEN("an order with zero quantity is submitted")
        {
            Order o{3, "TSLA", Side::Sell, 200.0, 0};

            THEN("it is rejected with a qty message")
            {
                REQUIRE(risk.check(o).value() == "qty must be positive");
            }
        }
    }
}

SCENARIO("risk engine boundary: order exactly at the limit", "[risk]")
{
    GIVEN("a risk engine with a 500,000 limit")
    {
        RiskEngine risk{500'000.0};

        WHEN("an order with notional exactly equal to the limit is submitted")
        {
            Order o{4, "SPY", Side::Buy, 500.0, 1000}; // exactly 500,000

            THEN("the limit is inclusive — order is approved")
            {
                REQUIRE_FALSE(risk.check(o).has_value());
            }
        }

        AND_WHEN("an order one unit above the limit is submitted")
        {
            Order o{5, "SPY", Side::Buy, 500.0, 1001}; // 500,500

            THEN("it is rejected")
            {
                REQUIRE(risk.check(o).has_value());
            }
        }
    }
}

SCENARIO("risk engine rejects malformed orders regardless of notional", "[risk]")
{
    GIVEN("a risk engine with an extremely high limit")
    {
        RiskEngine risk{1e12}; // limit won't be the cause of rejection

        WHEN("the price is zero")
        {
            THEN("the order is rejected")
            {
                REQUIRE(risk.check({6, "X", Side::Buy, 0.0, 10}).has_value());
            }
        }

        WHEN("the price is negative")
        {
            THEN("the order is rejected")
            {
                REQUIRE(risk.check({7, "X", Side::Buy, -50.0, 10}).has_value());
            }
        }

        WHEN("the quantity is negative")
        {
            THEN("the order is rejected")
            {
                REQUIRE(risk.check({8, "X", Side::Sell, 100.0, -5}).has_value());
            }
        }
    }
}
