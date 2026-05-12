#include <iostream>
#include <expected>  // C++23
#include <string>
#include <charconv>

// ─────────────────────────────────────────────────────────────────────
// std::expected<T, E> (C++23): the canonical result type.
// Either a T (success) or an E (error) — like variant<T,E> but with
// cleaner semantics and monadic operations for chaining.
//
// Construction:
//   return value;                  // implicit success
//   return std::unexpected(error); // explicit error
//
// Access:
//   .has_value()  — true if success
//   .value()      — T (throws std::bad_expected_access if error)
//   .error()      — E (UB if success — check has_value first)
//   .value_or(d)  — T or default
//
// Monadic chaining (avoids nested if-checks):
//   .and_then(f)   — call f(value) if success; f must return expected<U,E>
//   .transform(f)  — map f over the value; f returns U (not expected)
//   .or_else(f)    — call f(error) if error; f must return expected<T,F>
// ─────────────────────────────────────────────────────────────────────

struct ParseError { std::string msg; };
struct RangeError { int value; };

using IntResult = std::expected<int, ParseError>;

IntResult parse(std::string_view s)
{
    int result{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    if (ec != std::errc{})
        return std::unexpected(ParseError{"not an integer: " + std::string(s)});
    return result;
}

std::expected<int, RangeError> check_positive(int n)
{
    if (n <= 0) return std::unexpected(RangeError{n});
    return n;
}

// ── Chained pipeline ──────────────────────────────────────────────────
// parse → double the value → check it's positive
// If any step fails, the error short-circuits to the end.
// Note: and_then requires both steps to share the same error type.
// When types differ, handle each step explicitly (see main below).

std::expected<int, ParseError> parse_and_double(std::string_view s)
{
    return parse(s)
        .transform([](int n){ return n * 2; });  // map over value
}

int main()
{
    // ── Basic construction and access ─────────────────────────────────
    auto r1 = parse("42");
    auto r2 = parse("abc");

    std::cout << std::boolalpha;
    std::cout << "r1 has_value: " << r1.has_value() << "\n"; // true
    std::cout << "r1 value:     " << r1.value()     << "\n"; // 42
    std::cout << "r2 has_value: " << r2.has_value() << "\n"; // false
    std::cout << "r2 error:     " << r2.error().msg << "\n";

    // ── value_or ──────────────────────────────────────────────────────
    std::cout << "r2 value_or:  " << r2.value_or(-1) << "\n"; // -1

    // ── transform (map over success value) ───────────────────────────
    auto doubled = parse_and_double("7");
    std::cout << "doubled:      " << doubled.value() << "\n"; // 14

    // ── Multi-step: explicit check between steps (different E types) ──
    for (auto s : {"10", "-3", "xyz"}) {
        auto parsed = parse(s);
        if (!parsed) { std::cout << "parse error: " << parsed.error().msg << "\n"; continue; }

        auto checked = check_positive(*parsed);
        if (!checked) { std::cout << "range error: " << checked.error().value << " is not positive\n"; continue; }

        std::cout << "ok: " << *checked << "\n";
    }
}
