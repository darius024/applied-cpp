#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Writing testable code.
//
// The main obstacle to unit testing is hard-coded dependencies: singletons,
// global state, wall clocks, random numbers, I/O. The solution is a "seam"
// — an injection point where a test can substitute a controlled double.
//
// Test double taxonomy (Gerard Meszaros):
//   Dummy   — passed but never used; just satisfies a parameter list.
//   Stub    — returns canned data; no assertion on how many times called.
//   Fake    — lightweight working implementation (in-memory DB, fake clock).
//   Spy     — records calls for post-hoc inspection; no pre-set expectations.
//   Mock    — pre-programmed with call-count / argument expectations (gmock).
//
// Injection strategies:
//   Constructor injection  — dependency passed at construction (preferred).
//   Parameter injection    — passed to each method call that needs it.
//   Template injection     — compile-time; zero virtual overhead.
//
// Compile:  g++ -std=c++20 10_testability_patterns.cpp \
//               -lgtest_main -lgtest -pthread -o t10 && ./t10
// ─────────────────────────────────────────────────────────────────────

// ═════════════════════════════════════════════════════════════════════
// 1. Clock seam — interface injection
// ─────────────────────────────────────────────────────────────────────
// Hard-coded wall-clock calls make tests non-deterministic.
// Abstract the clock so tests inject a FakeClock.
// ═════════════════════════════════════════════════════════════════════

class IClock
{
public:
    virtual ~IClock() = default;
    using TimePoint = std::chrono::system_clock::time_point;
    virtual TimePoint now() const = 0;
};

// Production implementation — uses the real OS clock.
class SystemClock : public IClock
{
public:
    TimePoint now() const override { return std::chrono::system_clock::now(); }
};

// Fake: deterministic, controllable, zero OS calls.
class FakeClock : public IClock
{
public:
    explicit FakeClock(TimePoint t) : t_(t) {}
    void advance(std::chrono::seconds s) { t_ += s; }
    TimePoint now() const override { return t_; }
private:
    TimePoint t_;
};

// System under test — depends on IClock, not on the concrete system clock.
class TimestampedOrder
{
public:
    explicit TimestampedOrder(IClock& clock) : clock_(clock) {}

    std::string stamp(int id) const
    {
        auto epoch_secs = std::chrono::duration_cast<std::chrono::seconds>(
                              clock_.now().time_since_epoch()).count();
        return "order-" + std::to_string(id) + "@" + std::to_string(epoch_secs);
    }

private:
    IClock& clock_;
};

TEST(ClockSeamTest, StampIsReproducibleWithFakeClock)
{
    FakeClock clock{std::chrono::system_clock::time_point{
        std::chrono::seconds{1'000'000}}};

    TimestampedOrder order(clock);
    EXPECT_EQ(order.stamp(42), "order-42@1000000");

    clock.advance(std::chrono::seconds{5});
    EXPECT_EQ(order.stamp(42), "order-42@1000005"); // deterministic advance
}

// ═════════════════════════════════════════════════════════════════════
// 2. Stub — canned data, no call-count assertions
// ─────────────────────────────────────────────────────────────────────
// Write a stub when you need a return value but don't care whether
// or how many times the method is called. Simpler than a mock.
// ═════════════════════════════════════════════════════════════════════

class IPnlSource
{
public:
    virtual ~IPnlSource() = default;
    virtual double unrealised_pnl(int position_id) const = 0;
};

class StubPnlSource : public IPnlSource
{
public:
    explicit StubPnlSource(double fixed) : pnl_(fixed) {}
    double unrealised_pnl(int) const override { return pnl_; }
private:
    double pnl_;
};

class AutoHedge
{
public:
    AutoHedge(IPnlSource& src, double stop_loss) : src_(src), stop_(stop_loss) {}
    bool should_close(int pos_id) const { return src_.unrealised_pnl(pos_id) < -stop_; }
private:
    IPnlSource& src_;
    double stop_;
};

TEST(StubTest, ClosesWhenPnlBelowStopLoss)
{
    StubPnlSource stub{-5000.0};
    AutoHedge hedge(stub, 3000.0);
    EXPECT_TRUE(hedge.should_close(1));
}

TEST(StubTest, DoesNotCloseWhenPnlAboveStopLoss)
{
    StubPnlSource stub{-1000.0};
    AutoHedge hedge(stub, 3000.0);
    EXPECT_FALSE(hedge.should_close(1));
}

// ═════════════════════════════════════════════════════════════════════
// 3. Template injection — compile-time seam, zero virtual overhead
// ─────────────────────────────────────────────────────────────────────
// Use in hot paths (market data handlers, pricing loops) where a
// virtual call per tick would be expensive.
// ═════════════════════════════════════════════════════════════════════

template <typename Clock>
class LatencyMonitor
{
public:
    explicit LatencyMonitor(Clock& clock) : clock_(clock) {}

    long measure_us(std::function<void()> fn) const
    {
        auto start = clock_.now();
        fn();
        auto end = clock_.now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   end - start).count();
    }

private:
    Clock& clock_;
};

TEST(TemplateInjectionTest, ReportsElapsedMicroseconds)
{
    FakeClock clock{std::chrono::system_clock::time_point{}};
    LatencyMonitor<FakeClock> monitor(clock);

    long us = monitor.measure_us([&]{
        clock.advance(std::chrono::seconds{1}); // simulates 1 second of work
    });

    EXPECT_EQ(us, 1'000'000); // 1 s = 1,000,000 µs
}

// ═════════════════════════════════════════════════════════════════════
// 4. Spy — record calls for post-hoc assertions
// ─────────────────────────────────────────────────────────────────────
// Unlike a mock, a spy does not pre-declare expectations. You inspect
// recorded calls after the code under test has run.
// ═════════════════════════════════════════════════════════════════════

class IAuditLog
{
public:
    virtual ~IAuditLog() = default;
    virtual void write(const std::string& event) = 0;
};

class SpyAuditLog : public IAuditLog
{
public:
    void write(const std::string& event) override { entries_.push_back(event); }
    const std::vector<std::string>& entries() const { return entries_; }
    int count() const { return static_cast<int>(entries_.size()); }
private:
    std::vector<std::string> entries_;
};

class OrderProcessor
{
public:
    explicit OrderProcessor(IAuditLog& log) : log_(log) {}
    void process(int id) { log_.write("processed:" + std::to_string(id)); }
private:
    IAuditLog& log_;
};

TEST(SpyTest, RecordsOneEntryPerProcessedOrder)
{
    SpyAuditLog spy;
    OrderProcessor proc(spy);

    proc.process(1);
    proc.process(2);

    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.entries()[0], "processed:1");
    EXPECT_EQ(spy.entries()[1], "processed:2");
}

// ═════════════════════════════════════════════════════════════════════
// 5. Dummy — satisfies a required parameter but is never used
// ═════════════════════════════════════════════════════════════════════

class DummyAuditLog : public IAuditLog
{
public:
    void write(const std::string&) override {} // intentionally empty
};

TEST(DummyTest, ProcessorWorksWithDummyLog)
{
    DummyAuditLog dummy; // we don't care about logging in this test
    OrderProcessor proc(dummy);
    EXPECT_NO_THROW(proc.process(99)); // just verifying it doesn't throw
}
