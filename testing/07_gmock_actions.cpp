#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// GMock actions: control what a mock returns or does when called.
//
// Return(v)                  — return value v
// ReturnRef(ref)             — return a reference (must outlive the mock)
// ReturnArg<N>()             — return the Nth argument (0-indexed)
// ReturnPointee(ptr)         — dereference ptr and return its value
// Assign(&var, v)            — assign v to var (no return value)
// SaveArg<N>(&var)           — copy Nth arg to var
// SetArgPointee<N>(v)        — write v through the Nth pointer argument
// Invoke(f)                  — call f with the same arguments; return result
// InvokeWithoutArgs(f)       — call f(); return result
// DoAll(a1, ..., aLast)      — run all actions; last one provides the return
//
// WillOnce / WillRepeatedly:
//   .WillOnce(a1).WillOnce(a2).WillRepeatedly(a3)
//   — different action per successive call; a3 for all remaining calls.
//
// InSequence: enforce that EXPECT_CALLs fire in declaration order.
//
// Compile:  g++ -std=c++20 07_gmock_actions.cpp \
//               -lgmock -lgtest_main -lgtest -pthread -o t7 && ./t7
// ─────────────────────────────────────────────────────────────────────

using namespace ::testing;

// ── Interface ─────────────────────────────────────────────────────────

class IOrderRouter
{
public:
    virtual ~IOrderRouter() = default;
    virtual int  send(const std::string& symbol, int qty, double price) = 0;
    virtual bool cancel(int order_id) = 0;
    // Output parameter: writes fill price through the pointer.
    virtual bool query(int order_id, double* price_out) = 0;
};

class MockOrderRouter : public IOrderRouter
{
public:
    MOCK_METHOD(int,  send,   (const std::string&, int, double), (override));
    MOCK_METHOD(bool, cancel, (int),                             (override));
    MOCK_METHOD(bool, query,  (int, double*),                    (override));
};

// ── System under test ─────────────────────────────────────────────────

class SmartOrderRouter
{
public:
    explicit SmartOrderRouter(IOrderRouter& router) : router_(router) {}

    int  place(const std::string& sym, int qty, double price)
    { return router_.send(sym, qty, price); }

    bool try_cancel(int id) { return router_.cancel(id); }

    // Cancel old order and resend at a new price; returns new order id.
    int amend(int old_id, const std::string& sym, int qty, double new_price)
    {
        router_.cancel(old_id);
        return router_.send(sym, qty, new_price);
    }

    // Returns fill price if query succeeds, otherwise 0.
    double lookup_price(int id)
    {
        double price = 0.0;
        router_.query(id, &price);
        return price;
    }

private:
    IOrderRouter& router_;
};

// ═════════════════════════════════════════════════════════════════════
// Tests
// ═════════════════════════════════════════════════════════════════════

// ── Return: different value per successive call ───────────────────────
TEST(ActionTest, ReturnDifferentValuesPerCall)
{
    MockOrderRouter router;

    EXPECT_CALL(router, send(_, _, _))
        .WillOnce(Return(101))  // 1st call
        .WillOnce(Return(102)); // 2nd call

    SmartOrderRouter sor(router);
    EXPECT_EQ(sor.place("AAPL", 100, 150.0), 101);
    EXPECT_EQ(sor.place("AAPL", 100, 151.0), 102);
}

// ── WillRepeatedly: default action for all remaining calls ────────────
TEST(ActionTest, WillRepeatedlyAsDefault)
{
    MockOrderRouter router;

    EXPECT_CALL(router, send(_, _, _))
        .WillOnce(Return(1))
        .WillRepeatedly(Return(999)); // every call after the first

    SmartOrderRouter sor(router);
    EXPECT_EQ(sor.place("X", 1, 1.0), 1);
    EXPECT_EQ(sor.place("X", 1, 1.0), 999);
    EXPECT_EQ(sor.place("X", 1, 1.0), 999);
}

// ── DoAll + SaveArg: capture arguments for later inspection ───────────
TEST(ActionTest, SaveArgCapturesCallArguments)
{
    MockOrderRouter router;

    std::string captured_symbol;
    double      captured_price = 0.0;

    // DoAll runs all listed actions; the last one provides the return value.
    EXPECT_CALL(router, send(_, _, _))
        .WillOnce(DoAll(
            SaveArg<0>(&captured_symbol), // copy 0th arg (symbol)
            SaveArg<2>(&captured_price),  // copy 2nd arg (price)
            Return(42)
        ));

    SmartOrderRouter sor(router);
    EXPECT_EQ(sor.place("MSFT", 50, 300.0), 42);
    EXPECT_EQ(captured_symbol, "MSFT");
    EXPECT_DOUBLE_EQ(captured_price, 300.0);
}

// ── SetArgPointee: write through an output pointer parameter ──────────
TEST(ActionTest, SetArgPointeeWritesThroughOutputPointer)
{
    MockOrderRouter router;

    // The mock writes 195.50 through the double* that query receives.
    EXPECT_CALL(router, query(7, _))
        .WillOnce(DoAll(SetArgPointee<1>(195.50), Return(true)));

    SmartOrderRouter sor(router);
    EXPECT_DOUBLE_EQ(sor.lookup_price(7), 195.50);
}

// ── Invoke: delegate to a real function ──────────────────────────────
TEST(ActionTest, InvokeCallsCustomFunction)
{
    MockOrderRouter router;

    int next_id = 1000;
    // Invoke calls the lambda with the same args as the mock method.
    EXPECT_CALL(router, send(_, _, _))
        .WillRepeatedly(Invoke([&](const std::string&, int, double) {
            return ++next_id; // stateful: each call returns the next ID
        }));

    SmartOrderRouter sor(router);
    EXPECT_EQ(sor.place("X", 1, 1.0), 1001);
    EXPECT_EQ(sor.place("Y", 1, 2.0), 1002);
}

// ── InSequence: enforce call ordering ────────────────────────────────
// amend() must cancel the old order before sending the new one.
TEST(ActionTest, AmendCancelsBeforeResending)
{
    MockOrderRouter router;

    // InSequence guarantees that the EXPECT_CALLs fire in this exact order.
    InSequence seq;
    EXPECT_CALL(router, cancel(10)).WillOnce(Return(true));
    EXPECT_CALL(router, send("AAPL", 100, 151.0)).WillOnce(Return(11));

    SmartOrderRouter sor(router);
    EXPECT_EQ(sor.amend(10, "AAPL", 100, 151.0), 11);
}
