#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// GMock matchers: describe expected argument values precisely.
//
// Scalar:      Eq(v), Ne(v), Lt(v), Le(v), Gt(v), Ge(v)
//              DoubleNear(v, eps)  — |actual - v| ≤ eps
//              IsNull(), NotNull()
//              _ (anything)
//
// String:      HasSubstr(s), StartsWith(s), EndsWith(s)
//              MatchesRegex(re), ContainsRegex(re)
//
// Container:   IsEmpty(), SizeIs(n)
//              Contains(v)         — at least one element equals v
//              ElementsAre(...)    — exact contents, in order
//              UnorderedElementsAre(...) — exact contents, any order
//              Each(m)             — every element satisfies m
//              ContainerEq(c)      — equality with better error messages
//
// Object:      Field(&T::member, m)         — test a struct field
//              Property(&T::method, m)      — test a no-arg method return
//
// Composite:   AllOf(m1, m2, ...)  — all must pass
//              AnyOf(m1, m2, ...)  — at least one must pass
//              Not(m)
//
// Custom:      MATCHER(Name, desc) { return <predicate on 'arg'>; }
//              MATCHER_P(Name, param, desc) { ... }
//
// Compile:  g++ -std=c++20 06_gmock_matchers.cpp \
//               -lgmock -lgtest_main -lgtest -pthread -o t6 && ./t6
// ─────────────────────────────────────────────────────────────────────

using namespace ::testing;

// ── Types ─────────────────────────────────────────────────────────────

struct Trade {
    std::string symbol;
    double      price;
    int         qty;
    bool        is_buy;
};

class ITradeRecorder
{
public:
    virtual ~ITradeRecorder() = default;
    virtual void record(const Trade& t) = 0;
    virtual void record_batch(const std::vector<Trade>& trades) = 0;
    virtual void log(const std::string& msg) = 0;
};

class MockTradeRecorder : public ITradeRecorder
{
public:
    MOCK_METHOD(void, record,       (const Trade&),              (override));
    MOCK_METHOD(void, record_batch, (const std::vector<Trade>&), (override));
    MOCK_METHOD(void, log,          (const std::string&),        (override));
};

// ── Scalar matchers ───────────────────────────────────────────────────

TEST(ScalarMatcherTest, PriceAboveThreshold)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(Field(&Trade::price, Gt(100.0))));
    rec.record({"AAPL", 150.0, 10, true});
}

TEST(ScalarMatcherTest, DoubleNearForFloatArguments)
{
    // DoubleNear is essential when a float is computed before being passed.
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(Field(&Trade::price, DoubleNear(150.0, 0.01))));
    rec.record({"AAPL", 150.005, 10, true}); // within epsilon
}

// ── String matchers ───────────────────────────────────────────────────

TEST(StringMatcherTest, LogContainsSymbol)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, log(HasSubstr("TSLA")));
    rec.log("Trade executed: TSLA 100@200.5");
}

TEST(StringMatcherTest, LogStartsWithSeverityKeyword)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, log(StartsWith("ERROR")));
    rec.log("ERROR: order rejected by risk");
}

TEST(StringMatcherTest, LogEndsWithNewline)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, log(EndsWith("\n")));
    rec.log("filled 100 shares\n");
}

// ── Container matchers ────────────────────────────────────────────────

TEST(ContainerMatcherTest, BatchHasThreeTrades)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record_batch(SizeIs(3)));
    rec.record_batch({{"A", 1, 10, true}, {"B", 2, 20, false}, {"C", 3, 30, true}});
}

TEST(ContainerMatcherTest, BatchIsEmpty)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record_batch(IsEmpty()));
    rec.record_batch({});
}

TEST(ContainerMatcherTest, EachTradeHasPositiveQty)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record_batch(Each(Field(&Trade::qty, Gt(0)))));
    rec.record_batch({{"X", 1.0, 5, true}, {"Y", 2.0, 10, false}});
}

TEST(ContainerMatcherTest, BatchContainsASpecificSymbol)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record_batch(Contains(Field(&Trade::symbol, "SPY"))));
    rec.record_batch({{"SPY", 420.0, 100, true}, {"QQQ", 380.0, 50, false}});
}

// ── Object matchers: Field, AllOf, AnyOf ──────────────────────────────

TEST(ObjectMatcherTest, AllFieldsMatch)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(AllOf(
        Field(&Trade::symbol, "GOOG"),
        Field(&Trade::is_buy, true),
        Field(&Trade::qty,    Ge(1))
    )));
    rec.record({"GOOG", 2800.0, 5, true});
}

TEST(ObjectMatcherTest, EitherBuyOrLargeQty)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(AnyOf(
        Field(&Trade::is_buy, true),
        Field(&Trade::qty,    Gt(1000))
    )));
    rec.record({"AAPL", 150.0, 10, true}); // is_buy satisfies the condition
}

// ── Custom matcher ────────────────────────────────────────────────────
// MATCHER defines a named, reusable matcher. 'arg' is the argument.

MATCHER(IsValidTrade, "trade with positive price and positive qty")
{
    return arg.price > 0.0 && arg.qty > 0;
}

// MATCHER_P adds a parameter (e.g. a minimum notional threshold).
MATCHER_P(NotionalAbove, min_notional, "notional > min_notional")
{
    return arg.price * arg.qty > min_notional;
}

TEST(CustomMatcherTest, ValidTradePassesInvariant)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(IsValidTrade()));
    rec.record({"SPY", 420.0, 10, true});
}

TEST(CustomMatcherTest, NotionalThresholdMatcher)
{
    MockTradeRecorder rec;
    EXPECT_CALL(rec, record(NotionalAbove(10'000.0)));
    rec.record({"NVDA", 800.0, 50, true}); // notional = 40,000
}
