# Testing in C++

Automated tests give you a safety net for refactoring, documentation of intended
behaviour, and fast feedback when something breaks. In quantitative finance this
matters even more: a silent numerical error in a pricing engine or risk model can
be far more costly than a crash.

---

## The test pyramid

```
         ╔══════════╗
         ║  E2E /   ║   few, slow, high-confidence
         ║ system   ║
         ╚══════════╝
       ╔═════════════════╗
       ║   Integration   ║   moderate count, test wiring
       ╚═════════════════╝
    ╔═══════════════════════════╗
    ║        Unit tests         ║   many, fast, test logic in isolation
    ╚═══════════════════════════╝
```

**Unit tests** exercise a single function or class in complete isolation — all
external dependencies (clocks, I/O, network, random numbers) are replaced with
deterministic substitutes.

**Integration tests** exercise the wiring between two or more real components
(e.g. a pricing engine talking to a real database stub).

**End-to-end tests** run the full system with production-like configuration.

Keep the pyramid in mind: if your unit tests are slow or brittle it usually means
you are testing at the wrong level.

---

## Framework comparison

| Feature                     | Google Test / Mock | Catch2          | doctest         | Boost.Test     |
|-----------------------------|-------------------|-----------------|-----------------|----------------|
| Matchers & mocking          | ✓ (gmock)         | ✗ (use trompeloeil) | ✗           | ✗              |
| BDD syntax                  | ✗                 | ✓ (SCENARIO)    | ✓               | ✗              |
| Parameterized tests         | ✓ (TEST_P)        | ✓ (GENERATE)    | ✓               | ✓              |
| Header-only                 | ✗                 | ✓ (v2) / ✗ (v3) | ✓              | ✗              |
| Death tests                 | ✓                 | ✗               | ✗               | ✗              |
| Industry adoption           | Very high         | High            | Growing         | Legacy         |
| Install                     | `brew install googletest` | `brew install catch2` | header | cmake |

**Google Test + Mock** is the dominant choice in production C++ shops and is what
most quant teams use. **Catch2** excels at readable test output and BDD-style
specifications. This module covers both.

---

## Google Test

### Install & compile

```
macOS:   brew install googletest
Ubuntu:  apt install libgtest-dev libgmock-dev

Compile: g++ -std=c++20 my_test.cpp -lgtest_main -lgtest -pthread -o test && ./test
```

### TEST macro

```cpp
TEST(SuiteName, TestName) { /* body */ }
```

Suite and test names become the fully-qualified test name `SuiteName.TestName`.
No shared state — each `TEST` is completely independent.

### EXPECT_* vs ASSERT_*

| Macro family | On failure          | When to use                                      |
|--------------|---------------------|--------------------------------------------------|
| `EXPECT_*`   | Records, continues  | Default — see all failures at once               |
| `ASSERT_*`   | Records, aborts test| When continuing would dereference null or crash  |

### Assertions reference

| Assertion                    | Checks                                          |
|------------------------------|-------------------------------------------------|
| `EXPECT_EQ(a, b)`            | `a == b`                                        |
| `EXPECT_NE(a, b)`            | `a != b`                                        |
| `EXPECT_LT/LE/GT/GE(a, b)`  | Ordered comparisons                             |
| `EXPECT_NEAR(a, b, eps)`     | `|a - b| ≤ eps` — always use for floats         |
| `EXPECT_TRUE/FALSE(x)`       | Boolean check                                   |
| `EXPECT_THROW(expr, T)`      | `expr` throws exception of type `T`             |
| `EXPECT_NO_THROW(expr)`      | `expr` does not throw                           |
| `EXPECT_ANY_THROW(expr)`     | `expr` throws anything                          |
| `FAIL()` / `SUCCEED()`       | Unconditional failure / no-op success marker    |

**Never use `EXPECT_EQ` with floats.** Use `EXPECT_NEAR` or `EXPECT_DOUBLE_EQ`.

---

## Test fixtures: TEST_F

When multiple tests share the same setup, put it in a fixture class:

```cpp
class MyFixture : public ::testing::Test {
protected:
    void SetUp()    override { /* runs BEFORE each TEST_F */ }
    void TearDown() override { /* runs AFTER  each TEST_F */ }
    MyClass obj_;   // reset for every test
};

TEST_F(MyFixture, SomeBehaviour) { /* obj_ is fresh */ }
```

Each `TEST_F` gets a brand-new instance of the fixture — mutations in one test
cannot pollute another. This is stronger isolation than module-level globals.

**Suite-level setup** (`SetUpTestSuite` / `TearDownTestSuite`): runs once per
suite. Use for expensive shared resources (loading large market data files,
opening DB connections). Members must be `static`; tests must not mutate them.

---

## Parameterized tests

### Value-parameterized (TEST_P)

Run the same test body over many inputs without duplicating code:

```cpp
class MyTest : public ::testing::TestWithParam<int> {};

TEST_P(MyTest, IsEven) { EXPECT_EQ(GetParam() % 2, 0); }

INSTANTIATE_TEST_SUITE_P(Even, MyTest, ::testing::Values(0, 2, 4, 6));
```

Generators: `Values(...)`, `ValuesIn(container)`, `Range(a, b, step)`,
`Combine(g1, g2)` (Cartesian product).

### Typed tests (TYPED_TEST)

Run the same test body for a compile-time list of types — useful for generic
containers or numeric functions that must work for `float`, `double`, `long double`:

```cpp
template <typename T> class MyTest : public ::testing::Test {};
using Types = ::testing::Types<float, double>;
TYPED_TEST_SUITE(MyTest, Types);
TYPED_TEST(MyTest, Positive) { EXPECT_GT(TypeParam{1}, TypeParam{0}); }
```

---

## Death tests

**Death tests** verify that code terminates as expected (assertion violations,
`abort()`, uncaught signals). They run in a child process so the test suite
survives.

```cpp
EXPECT_DEATH(statement, stderr_regex);     // terminates with any signal
EXPECT_EXIT(statement, predicate, regex);  // checks exit code too
```

Name your death-test suite with the suffix `DeathTest` — gtest runs all death
tests before regular tests to avoid process-level side effects.

> Compile without `-DNDEBUG` so `assert()` is active.

---

## Google Mock

### Install & compile

```
macOS:   included with googletest (brew install googletest)

Compile: g++ -std=c++20 my_test.cpp -lgmock -lgtest_main -lgtest -pthread -o test
```

### Creating a mock

```cpp
class IFeed { public: virtual double price(const std::string& sym) const = 0; };

class MockFeed : public IFeed {
public:
    MOCK_METHOD(double, price, (const std::string&), (const, override));
};
```

`MOCK_METHOD(RetType, Name, (Args), (Qualifiers))` generates the method and the
bookkeeping needed to record calls.

### EXPECT_CALL vs ON_CALL

| Macro         | Records call count? | Fails if not called? | Use when                      |
|---------------|---------------------|----------------------|-------------------------------|
| `EXPECT_CALL` | ✓                   | ✓ (by default)       | Call is part of the contract  |
| `ON_CALL`     | ✗                   | ✗                    | Setting a default return value |

```cpp
EXPECT_CALL(mock, price("AAPL"))
    .Times(2)
    .WillRepeatedly(Return(150.0));
```

**Set expectations before running the code under test, never after.**

### Matchers

Matchers describe expected argument values. They compose with `AllOf`, `AnyOf`, `Not`:

| Matcher                    | Checks                                           |
|----------------------------|--------------------------------------------------|
| `_`                        | Any value                                        |
| `Eq(v)`, `Ne(v)`, `Lt(v)`  | Scalar comparisons                               |
| `DoubleNear(v, eps)`       | Floating-point near                              |
| `HasSubstr(s)`             | String contains s                                |
| `StartsWith(s)`            | String prefix                                    |
| `ElementsAre(v1, v2, ...)` | Container exact contents, in order               |
| `UnorderedElementsAre(...)`| Container exact contents, any order              |
| `SizeIs(n)`                | Container size                                   |
| `Each(m)`                  | Every element matches m                          |
| `Field(&T::f, m)`          | Struct field matches m                           |
| `Property(&T::f, m)`       | No-arg method return matches m                   |
| `AllOf(m1, m2)`            | All matchers pass                                |
| `Not(m)`                   | Matcher fails                                    |

Custom matchers:

```cpp
MATCHER(IsPositiveQty, "qty > 0") { return arg.qty > 0; }
```

### Actions

Actions control what the mock does when called:

| Action                      | Effect                                           |
|-----------------------------|--------------------------------------------------|
| `Return(v)`                 | Return value v                                   |
| `ReturnRef(ref)`            | Return a reference                               |
| `ReturnArg<N>()`            | Return the Nth argument                          |
| `SaveArg<N>(&var)`          | Copy Nth arg to var                              |
| `SetArgPointee<N>(v)`       | Write v through Nth pointer arg                  |
| `Invoke(f)`                 | Call f with same arguments                       |
| `InvokeWithoutArgs(f)`      | Call f()                                         |
| `DoAll(a1, ..., aLast)`     | Run all; last provides return value              |

**Ordering**: `InSequence seq;` inside a test enforces that `EXPECT_CALL`s in
that scope fire in the order they were written.

---

## Catch2

### Install & compile

```
macOS:   brew install catch2

Compile (v3): g++ -std=c++20 my_test.cpp -lCatch2Main -lCatch2 -o test && ./test
```

### TEST_CASE and SECTION

```cpp
TEST_CASE("description", "[tag]")
{
    // shared setup code here (runs fresh before each SECTION)

    SECTION("path A") { /* ... */ }
    SECTION("path B") { /* ... */ }
}
```

Each `SECTION` re-runs the shared setup above it. This replaces fixture classes —
setup is just normal code. Sections can nest: inner sections re-run their parent.

### REQUIRE vs CHECK

| Macro      | On failure          | Equivalent gtest  |
|------------|---------------------|-------------------|
| `REQUIRE`  | Abort current test  | `ASSERT_*`        |
| `CHECK`    | Continue            | `EXPECT_*`        |

### Floating-point

```cpp
REQUIRE(value == Approx(expected));               // relative: 1e-3
REQUIRE(value == Approx(expected).epsilon(1e-9)); // relative tolerance
REQUIRE(value == Approx(0.0).margin(1e-12));      // absolute tolerance
```

### Exception assertions

```cpp
REQUIRE_THROWS_AS(expr, ExceptionType);
REQUIRE_THROWS_WITH(expr, "message substring");
REQUIRE_NOTHROW(expr);
```

---

## BDD with Catch2

BDD (Behaviour-Driven Development) expresses tests as plain-English stories.
`SCENARIO / GIVEN / WHEN / THEN` are aliases for `TEST_CASE / SECTION` — same
semantics, more communicative intent.

```cpp
SCENARIO("risk engine blocks over-limit orders", "[risk]")
{
    GIVEN("a risk limit of 1,000,000")
    {
        RiskEngine risk{1'000'000};

        WHEN("an order with notional 1,500,000 is submitted")
        {
            THEN("it is rejected") { REQUIRE(risk.check(order).has_value()); }
        }
    }
}
```

Use BDD when the test audience includes non-programmers (quant analysts,
compliance, product owners).

---

## Testability patterns

### Test double taxonomy (Gerard Meszaros)

| Double  | Has logic? | Assertions? | Use when                                     |
|---------|------------|-------------|----------------------------------------------|
| Dummy   | ✗          | ✗           | Filling a required parameter you won't use   |
| Stub    | Minimal    | ✗           | Returning canned data without caring about calls |
| Fake    | Yes        | ✗           | Lightweight working implementation (in-memory DB, fake clock) |
| Spy     | Yes        | Post-hoc    | Recording calls for later inspection         |
| Mock    | gmock      | Pre-defined | Pre-programmed expectations on call count/args |

### Dependency injection

The most effective way to insert seams. Pass dependencies through the constructor
— they can be real in production, substitutes in tests.

```cpp
// UNTESTABLE: hard-coded dependency
struct Pricer { double price(const std::string& sym) {
    return LiveFeed::instance().get(sym); }   // singleton
};

// TESTABLE: injected dependency
struct Pricer { explicit Pricer(IFeed& feed) : feed_(feed) {}
    double price(const std::string& sym) { return feed_.get(sym); }
    IFeed& feed_;
};
```

### Seams for non-injectable dependencies

Some dependencies can't be injected through interfaces cheaply — time, randomness,
system calls. Wrap them in a thin interface or use template injection:

```cpp
// Interface seam (virtual): testable with FakeClock
class IClock { public: virtual time_point now() const = 0; };

// Template seam (no virtual): testable with any type satisfying the concept
template <typename Clock>
class LatencyMonitor { explicit LatencyMonitor(Clock& c); };
```

Template injection has zero runtime cost and is preferred in hot paths.

---

## Examples

| File                          | Framework    | Covers                                                          |
|-------------------------------|--------------|-----------------------------------------------------------------|
| `01_gtest_basics.cpp`         | gtest        | `TEST`, `EXPECT_*`, `ASSERT_*`, `EXPECT_NEAR`, throw/no-throw  |
| `02_gtest_fixtures.cpp`       | gtest        | `TEST_F`, `SetUp`, `TearDown`, `SetUpTestSuite`                 |
| `03_gtest_parameterized.cpp`  | gtest        | `TEST_P`, `INSTANTIATE_TEST_SUITE_P`, `Combine`, `TYPED_TEST`   |
| `04_gtest_death_tests.cpp`    | gtest        | `EXPECT_DEATH`, `EXPECT_EXIT`, `EXPECT_THROW`                   |
| `05_gmock_basics.cpp`         | gmock        | `MOCK_METHOD`, `EXPECT_CALL`, `ON_CALL`, `Times`, `NiceMock`    |
| `06_gmock_matchers.cpp`       | gmock        | `Field`, `AllOf`, `Each`, `DoubleNear`, custom `MATCHER`        |
| `07_gmock_actions.cpp`        | gmock        | `DoAll`, `SaveArg`, `SetArgPointee`, `Invoke`, `InSequence`     |
| `08_catch2_basics.cpp`        | Catch2       | `TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`, `Approx`            |
| `09_catch2_bdd.cpp`           | Catch2       | `SCENARIO`, `GIVEN`, `WHEN`, `THEN`, `AND_WHEN`                 |
| `10_testability_patterns.cpp` | gtest        | Clock seam, stub, spy, template injection, fake                 |
