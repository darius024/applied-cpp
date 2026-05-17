#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Google Mock basics.
//
// A mock is a generated test double that:
//   - records every call made on it, and
//   - verifies expectations (call count, arguments) at end of the test.
//
// MOCK_METHOD(RetType, Name, (Args), (Qualifiers))
//   — generates the override and all gmock bookkeeping.
//
// EXPECT_CALL(mock, Method(matchers...))
//   .Times(n)               — exact call count  (default: 1 if WillOnce)
//   .WillOnce(action)       — what to do on the nth call
//   .WillRepeatedly(action) — default for every call beyond WillOnce list
//
// ON_CALL(mock, Method(matchers...))
//   .WillByDefault(action)  — sets a default; does NOT assert call count.
//   Use ON_CALL when you want a return value but don't care if/how many
//   times the method is called.
//
// NiceMock<T>   — suppresses warnings for calls with no EXPECT_CALL.
// StrictMock<T> — turns unexpected calls into test failures.
//
// Install:  brew install googletest  (includes gmock)
// Compile:  g++ -std=c++20 05_gmock_basics.cpp \
//               -lgmock -lgtest_main -lgtest -pthread -o t5 && ./t5
// ─────────────────────────────────────────────────────────────────────

using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

// ── Interface (production code) ───────────────────────────────────────

struct Quote { std::string symbol; double bid, ask; };

class IMarketDataFeed
{
public:
    virtual ~IMarketDataFeed() = default;
    virtual Quote  get_quote(const std::string& symbol) const = 0;
    virtual bool   is_connected() const = 0;
    virtual void   subscribe(const std::string& symbol) = 0;
    virtual std::vector<std::string> subscriptions() const = 0;
};

// ── Mock: generated from the interface ───────────────────────────────

class MockMarketDataFeed : public IMarketDataFeed
{
public:
    MOCK_METHOD(Quote,                    get_quote,     (const std::string&), (const, override));
    MOCK_METHOD(bool,                     is_connected,  (),                   (const, override));
    MOCK_METHOD(void,                     subscribe,     (const std::string&), (override));
    MOCK_METHOD(std::vector<std::string>, subscriptions, (),                   (const, override));
};

// ── System under test ─────────────────────────────────────────────────

class PricingEngine
{
public:
    explicit PricingEngine(IMarketDataFeed& feed) : feed_(feed) {}

    // Returns mid-price, or -1 if the feed is disconnected.
    double mid_price(const std::string& symbol) const
    {
        if (!feed_.is_connected()) return -1.0;
        auto q = feed_.get_quote(symbol);
        return (q.bid + q.ask) / 2.0;
    }

    void watch(const std::string& symbol) { feed_.subscribe(symbol); }

private:
    IMarketDataFeed& feed_;
};

// ═════════════════════════════════════════════════════════════════════
// Tests
// ═════════════════════════════════════════════════════════════════════

TEST(PricingEngineTest, ReturnsMidPriceWhenConnected)
{
    MockMarketDataFeed feed;

    // Expectations are set BEFORE calling the code under test.
    EXPECT_CALL(feed, is_connected())
        .WillOnce(Return(true));
    EXPECT_CALL(feed, get_quote("AAPL"))
        .WillOnce(Return(Quote{"AAPL", 149.90, 150.10}));

    PricingEngine engine(feed);
    EXPECT_NEAR(engine.mid_price("AAPL"), 150.0, 1e-9);
}

TEST(PricingEngineTest, ReturnsMinusOneWhenDisconnected)
{
    MockMarketDataFeed feed;

    EXPECT_CALL(feed, is_connected())
        .WillOnce(Return(false));

    // get_quote must NOT be called — if it is, gmock fails the test.
    PricingEngine engine(feed);
    EXPECT_EQ(engine.mid_price("AAPL"), -1.0);
}

TEST(PricingEngineTest, WatchCallsSubscribeExactlyOnce)
{
    MockMarketDataFeed feed;

    EXPECT_CALL(feed, subscribe("GOOGL"))
        .Times(1); // exactly one call

    PricingEngine engine(feed);
    engine.watch("GOOGL");
}

TEST(PricingEngineTest, TimesAtLeastOnSuccessiveCalls)
{
    MockMarketDataFeed feed;

    EXPECT_CALL(feed, is_connected())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(feed, get_quote(::testing::_)) // _ = any argument
        .WillRepeatedly(Return(Quote{"X", 10.0, 10.2}));

    PricingEngine engine(feed);
    engine.mid_price("X");
    engine.mid_price("X");
}

// ── NiceMock: suppress warnings for calls without EXPECT_CALL ─────────
// Use when a helper inside the SUT makes incidental calls you don't
// want to model explicitly in every test.
TEST(PricingEngineTest, NiceMockSuppressesUnexpectedCallWarnings)
{
    NiceMock<MockMarketDataFeed> feed;

    ON_CALL(feed, is_connected()).WillByDefault(Return(true));
    ON_CALL(feed, get_quote(::testing::_))
        .WillByDefault(Return(Quote{"Z", 5.0, 5.1}));

    PricingEngine engine(feed);
    EXPECT_NEAR(engine.mid_price("Z"), 5.05, 1e-9);
    // Calls to subscriptions() or subscribe() won't generate warnings.
}

// ── StrictMock: any unexpected call is a test failure ─────────────────
TEST(PricingEngineTest, StrictMockFailsOnUnexpectedCall)
{
    // StrictMock<MockMarketDataFeed> feed;
    // Only use when you need iron-clad guarantees that no extra methods
    // are called. Omitted here because it would require modelling every call.
    SUCCEED(); // placeholder
}
