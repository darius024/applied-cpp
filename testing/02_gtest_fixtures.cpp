#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Test fixtures: TEST_F.
//
// When multiple tests share the same setup or teardown, put it in a
// fixture class that inherits from ::testing::Test.
//
//   class MyFixture : public ::testing::Test {
//   protected:
//       void SetUp()    override { /* runs BEFORE each TEST_F */ }
//       void TearDown() override { /* runs AFTER  each TEST_F */ }
//       MyClass obj_;  // reset to a fresh instance for every test
//   };
//
// Each TEST_F gets its own fixture instance — no state leaks between tests.
//
// SetUpTestSuite / TearDownTestSuite: run ONCE per suite, not per test.
//   Use for expensive shared initialisation (large files, DB connections).
//   Members must be static; individual tests must not mutate them.
//
// Compile:  g++ -std=c++20 02_gtest_fixtures.cpp \
//               -lgtest_main -lgtest -pthread -o t2 && ./t2
// ─────────────────────────────────────────────────────────────────────

// ── Subject under test: simple limit order book ───────────────────────

struct Order { int id; double price; int qty; };

class OrderBook
{
public:
    void add(Order o)
    {
        if (o.qty <= 0) throw std::invalid_argument("qty must be positive");
        levels_[o.price].push_back(o);
    }

    bool cancel(int order_id)
    {
        for (auto& [price, orders] : levels_) {
            auto it = std::find_if(orders.begin(), orders.end(),
                                   [&](const Order& o){ return o.id == order_id; });
            if (it != orders.end()) { orders.erase(it); return true; }
        }
        return false;
    }

    double best_bid() const
    {
        if (levels_.empty()) throw std::runtime_error("empty book");
        return levels_.rbegin()->first; // highest price key
    }

    int depth() const
    {
        int total = 0;
        for (auto& [p, orders] : levels_) total += static_cast<int>(orders.size());
        return total;
    }

private:
    std::map<double, std::vector<Order>> levels_;
};

// ═════════════════════════════════════════════════════════════════════
// Per-test fixture
// ─────────────────────────────────────────────────────────────────────
// SetUp adds three resting orders. Each TEST_F starts from this state.
// Mutations made by one test do NOT affect the next.
// ═════════════════════════════════════════════════════════════════════

class OrderBookTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        book_.add({1, 99.50, 100});
        book_.add({2, 99.75, 200});
        book_.add({3, 99.25, 150});
    }

    // TearDown() is optional when the destructor handles cleanup.
    void TearDown() override { /* nothing heap-allocated here */ }

    OrderBook book_;
};

TEST_F(OrderBookTest, BestBidIsHighestPrice)
{
    EXPECT_DOUBLE_EQ(book_.best_bid(), 99.75);
}

TEST_F(OrderBookTest, DepthCountsAllOrders)
{
    EXPECT_EQ(book_.depth(), 3);
}

TEST_F(OrderBookTest, CancelReducesDepth)
{
    // ASSERT: if cancel fails, the subsequent depth check would be misleading.
    ASSERT_TRUE(book_.cancel(1));
    EXPECT_EQ(book_.depth(), 2);
}

TEST_F(OrderBookTest, CancelReturnsFalseForUnknownId)
{
    EXPECT_FALSE(book_.cancel(999));
    EXPECT_EQ(book_.depth(), 3); // book is unchanged
}

TEST_F(OrderBookTest, AddRejectsZeroQty)
{
    EXPECT_THROW(book_.add({4, 100.0, 0}), std::invalid_argument);
    EXPECT_EQ(book_.depth(), 3); // the failed add must not corrupt state
}

TEST_F(OrderBookTest, AddRejectsNegativeQty)
{
    EXPECT_THROW(book_.add({5, 100.0, -10}), std::invalid_argument);
}

TEST_F(OrderBookTest, BestBidUpdatesAfterCancel)
{
    ASSERT_TRUE(book_.cancel(2)); // remove the 99.75 order
    EXPECT_DOUBLE_EQ(book_.best_bid(), 99.50); // best is now 99.50
}

// ═════════════════════════════════════════════════════════════════════
// Suite-level setup: run once for the entire suite
// ─────────────────────────────────────────────────────────────────────
// Use when initialisation is expensive (loading a pricing curve from disk,
// spinning up an in-process server). Individual tests share the resource
// but must treat it as read-only.
// ═════════════════════════════════════════════════════════════════════

class PricingCurveTest : public ::testing::Test
{
protected:
    // Called once before the first test in this suite.
    static void SetUpTestSuite()
    {
        // Simulate expensive curve bootstrap (e.g. spline fit on 500 knots).
        curve_value_ = 0.045; // canned result for a 5y swap rate
    }

    // Called once after the last test in this suite.
    static void TearDownTestSuite()
    {
        curve_value_ = 0.0;
    }

    static double curve_value_;
};

double PricingCurveTest::curve_value_ = 0.0;

TEST_F(PricingCurveTest, CurveValueIsPositive)
{
    EXPECT_GT(curve_value_, 0.0);
}

TEST_F(PricingCurveTest, CurveValueIsInReasonableRange)
{
    // A 5y swap rate is rarely below 0 or above 20%.
    EXPECT_GE(curve_value_, 0.0);
    EXPECT_LE(curve_value_, 0.20);
}
