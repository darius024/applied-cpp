#include <iostream>
#include <type_traits>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// if constexpr — compile-time branching inside a template (C++17).
//
// The DISCARDED branch is never instantiated. This means it can contain
// code that would not compile for other types — unlike a regular if,
// where both branches must be well-formed for every T.
//
// Replaces many SFINAE patterns with much cleaner code.
// The compiler still type-checks the discarded branch for syntax, but
// does not instantiate it, so type-dependent errors in the discarded
// branch don't fire.
// ─────────────────────────────────────────────────────────────────────


// ── Replacing enable_if-based overloads ──────────────────────────────
//
// Before (SFINAE, two overloads): see 04_sfinae.cpp describe().
// After (if constexpr, one function):

template<typename T>
std::string describe(T)
{
    if constexpr (std::is_integral_v<T>)
        return "integral";
    else if constexpr (std::is_floating_point_v<T>)
        return "floating point";
    else
        return "other";
}


// ── Calling type-specific methods only when they exist ────────────────
//
// Without if constexpr, calling .size() on int would be a compile error
// even inside a branch that's never reached for int.

template<typename T>
void print_info(const T& v)
{
    std::cout << "value: " << v;
    if constexpr (requires { v.size(); })   // ad-hoc requires (C++20 expr)
        std::cout << " size=" << v.size();
    std::cout << "\n";
}


// ── Recursive variadic with if constexpr ─────────────────────────────
//
// Before C++17: required a separate base-case overload.
// With if constexpr: base case is inside the function.

template<typename T, typename... Rest>
void print_all(const T& head, const Rest&... rest)
{
    std::cout << head;
    if constexpr (sizeof...(rest) > 0) {
        std::cout << ", ";
        print_all(rest...);         // only instantiated when pack is non-empty
    } else {
        std::cout << "\n";
    }
}


// ── Serialise: different code paths per type ──────────────────────────

template<typename T>
void serialise(const T& v)
{
    if constexpr (std::is_same_v<T, bool>) {
        std::cout << (v ? "true" : "false");

    } else if constexpr (std::is_arithmetic_v<T>) {
        std::cout << v;

    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << '"' << v << '"';

    } else {
        // This line would fail to compile for arithmetic types (no .begin()),
        // but it's only instantiated when T is none of the above.
        std::cout << "[";
        for (const auto& item : v) { serialise(item); std::cout << " "; }
        std::cout << "]";
    }
    std::cout << "\n";
}


int main()
{
    // ── describe ─────────────────────────────────────────────────────
    std::cout << describe(10)    << "\n"; // integral
    std::cout << describe(3.14)  << "\n"; // floating point
    std::cout << describe("hi")  << "\n"; // other

    // ── print_info ───────────────────────────────────────────────────
    std::cout << "\n";
    print_info(42);
    print_info(std::string("hello"));
    std::vector<int> vec{1,2,3};
    print_info(vec);

    // ── print_all ────────────────────────────────────────────────────
    std::cout << "\n";
    print_all(1, "two", 3.0);
    print_all(42);

    // ── serialise ────────────────────────────────────────────────────
    std::cout << "\n";
    serialise(true);
    serialise(42);
    serialise(std::string("hello"));
    serialise(std::vector<int>{1, 2, 3});
}
