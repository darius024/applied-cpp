#include <iostream>
#include <string>
#include <vector>
#include <concepts>

// ─────────────────────────────────────────────────────────────────────
// Concepts (C++20) — named compile-time predicates on types.
//
// Before concepts, template errors fired deep inside instantiation stacks
// (SFINAE, enable_if). Concepts move the error to the CALL SITE and give
// a human-readable message.
//
// Three ways to apply a concept:
//   1. Abbreviated function template:  void f(Concept auto x)
//   2. Requires clause:                template<typename T> requires Concept<T> void f(T)
//   3. Constrained template parameter: template<Concept T> void f(T)
//
// All three are equivalent; pick for readability.
// ─────────────────────────────────────────────────────────────────────


// ── Defining a concept ───────────────────────────────────────────────
//
// A concept is a boolean constexpr predicate over template parameters.
// The requires-expression inside lists expressions that must be valid.

template<typename T>
concept Printable = requires(T t) {
    { std::cout << t } -> std::same_as<std::ostream&>; // must be streamable
};

// Defined in terms of standard concepts so subsumption works:
// `std::integral<T>` properly refines `Numeric<T>` in overload resolution.
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// A more complex concept: a type that supports +, -, * and has a default ctor.
template<typename T>
concept Arithmetic = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    T{};                                   // default constructible
};

// Composing concepts with && and ||
template<typename T>
concept PrintableNumeric = Printable<T> && Numeric<T>;


// ── Using concepts ────────────────────────────────────────────────────

// Style 1: abbreviated (cleanest for simple cases)
void print(const Printable auto& v)
{
    std::cout << v << "\n";
}

// Style 2: requires clause (preferred when concept involves multiple params)
template<typename T>
    requires Arithmetic<T>
T add(T a, T b) { return a + b; }

// Style 3: constrained template parameter
template<Numeric T>
T square(T x) { return x * x; }


// ── Concept-based overloading ─────────────────────────────────────────
//
// The compiler picks the most constrained overload. More constrained
// means more requirements — it's selected when its constraints are
// satisfied AND more specific than any other candidate.

template<typename T>
void describe(T) { std::cout << "generic\n"; }

template<Numeric T>
void describe(T) { std::cout << "numeric\n"; }

template<std::integral T>
void describe(T) { std::cout << "integral\n"; } // more constrained than Numeric


// ── Concepts on class templates ───────────────────────────────────────

template<Numeric T>
class NumericPair
{
public:
    NumericPair(T a, T b) : a_(a), b_(b) {}
    T sum()     const { return a_ + b_; }
    T product() const { return a_ * b_; }
private:
    T a_, b_;
};


int main()
{
    // ── print ─────────────────────────────────────────────────────────
    print(42);
    print(3.14);
    print(std::string("hello"));
    // print(std::vector<int>{});  // compile error: vector is not Printable

    // ── add / square ──────────────────────────────────────────────────
    std::cout << add(3, 4)       << "\n";  // 7
    std::cout << add(1.5, 2.5)   << "\n";  // 4.0
    std::cout << square(5)       << "\n";  // 25

    // ── overload resolution ───────────────────────────────────────────
    std::cout << "\n";
    describe(std::string("hi")); // generic
    describe(3.14);              // numeric
    describe(42);                // integral  ← most constrained wins

    // ── class template ────────────────────────────────────────────────
    std::cout << "\n";
    NumericPair<int> p(3, 4);
    std::cout << "sum=" << p.sum() << " product=" << p.product() << "\n";
    // NumericPair<std::string> bad("a","b"); // compile error: string is not Numeric
}
