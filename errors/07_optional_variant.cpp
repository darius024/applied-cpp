#include <iostream>
#include <optional>
#include <variant>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// std::optional<T>: a value that may or may not be present.
// Use instead of returning sentinels (-1, nullptr, "") for functions
// that can legitimately "find nothing".
//
// std::variant<Ts...>: a type-safe union holding exactly one of Ts.
// As an error type: variant<Result, Error> carries either a result or
// an error; the type itself encodes which one is present.
// ─────────────────────────────────────────────────────────────────────

// ── optional: nullable result ─────────────────────────────────────────
std::optional<int> find_first_even(const std::vector<int>& v)
{
    for (int x : v)
        if (x % 2 == 0) return x; // implicit construction from T
    return std::nullopt;           // explicit "empty"
}

std::optional<std::string> get_env(const std::string& key)
{
    const char* val = std::getenv(key.c_str());
    if (!val) return std::nullopt;
    return std::string(val);
}

// ── variant as an error union ─────────────────────────────────────────
struct ParseError { std::string msg; };

std::variant<int, ParseError> parse_int(const std::string& s)
{
    try   { return std::stoi(s); }
    catch (...) { return ParseError{"not an integer: " + s}; }
}

int main()
{
    // ── optional: three access styles ────────────────────────────────
    std::vector<int> odds  = {1, 3, 5};
    std::vector<int> mixed = {1, 3, 4, 6};

    auto r1 = find_first_even(odds);
    auto r2 = find_first_even(mixed);

    // value_or: safe, never throws
    std::cout << "odds first even: "  << r1.value_or(-1) << "\n"; // -1
    std::cout << "mixed first even: " << r2.value_or(-1) << "\n"; // 4

    // operator bool / operator*
    if (r2) std::cout << "found: " << *r2 << "\n"; // 4

    // value() throws std::bad_optional_access if empty
    try { r1.value(); }
    catch (const std::bad_optional_access&) {
        std::cout << "r1 is empty\n";
    }

    // ── optional from environment ─────────────────────────────────────
    auto home = get_env("HOME");
    std::cout << "HOME: " << home.value_or("(not set)") << "\n";

    // ── variant: pattern matching with std::visit ─────────────────────
    for (const std::string& s : {"42", "abc", "-7"}) {
        auto result = parse_int(s);
        std::visit([](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int>)
                std::cout << "parsed: " << v << "\n";
            else
                std::cout << "error: " << v.msg << "\n";
        }, result);
    }
}
