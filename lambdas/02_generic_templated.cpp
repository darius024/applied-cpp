#include <iostream>
#include <vector>
#include <string>
#include <concepts>

// ─────────────────────────────────────────────────────────────────────
// Generic lambdas (C++14): use `auto` for parameters.
// The compiler generates a templated operator() — one instantiation per
// unique argument type combination, just like a function template.
//
// Templated lambdas (C++20): explicit <typename T> syntax.
// Needed when:
//   - you must name T to use it in multiple places
//   - you want to constrain T with a concept
//   - you need to relate two parameters by the same type
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Generic lambda (C++14) ────────────────────────────────────────
    // `auto` params: works for any type supporting operator<<.
    auto print = [](const auto& v) { std::cout << v << "\n"; };
    print(42);
    print(3.14);
    print(std::string("hello"));

    // Generic lambda with multiple auto params — each can be a different type.
    auto add = [](auto a, auto b) { return a + b; };
    std::cout << add(1, 2)          << "\n"; // int
    std::cout << add(1.5, 2.5)      << "\n"; // double
    std::cout << add(std::string("foo"), std::string("bar")) << "\n";

    // ── Templated lambda (C++20) ──────────────────────────────────────
    // Explicit <typename T>: T is named, so it can be used in multiple places.

    // Enforce both parameters are the SAME type.
    // With `auto, auto` you'd silently accept mismatched types.
    auto same_type_add = []<typename T>(T a, T b) { return a + b; };
    std::cout << same_type_add(3, 4)     << "\n"; // ok: both int
    // same_type_add(1, 2.0)             // compile error: int vs double

    // Templated lambda with concept constraint.
    auto numeric_square = []<std::integral T>(T x) { return x * x; };
    std::cout << numeric_square(7) << "\n"; // 49
    // numeric_square(3.14)        // compile error: double is not integral

    // Access the element type of a container — impossible cleanly with auto.
    auto sum_container = []<typename Container>(const Container& c) {
        typename Container::value_type acc{};
        for (const auto& x : c) acc += x;
        return acc;
    };

    std::vector<int>    vi = {1, 2, 3, 4};
    std::vector<double> vd = {1.1, 2.2, 3.3};
    std::cout << sum_container(vi) << "\n"; // 10
    std::cout << sum_container(vd) << "\n"; // 6.6

    // ── Recursive generic lambda ──────────────────────────────────────
    // A lambda cannot refer to itself by name directly.
    // Pass itself as a parameter — common pattern for inline recursion.
    auto factorial = []<typename Self>(Self&& self, int n) -> int {
        return n <= 1 ? 1 : n * self(self, n - 1);
    };
    std::cout << factorial(factorial, 6) << "\n"; // 720

    // C++23 adds `this` deduction (deducing this) which cleans this up,
    // but the above pattern works in C++20 today.
}
