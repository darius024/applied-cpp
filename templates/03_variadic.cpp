#include <iostream>
#include <string>
#include <tuple>

// ─────────────────────────────────────────────────────────────────────
// Variadic templates accept any number of type parameters.
//
//   template<typename... Ts>   — Ts is a parameter pack
//   sizeof...(Ts)              — number of types in the pack
//   Ts...                      — expand the pack (must always be followed by ...)
//
// Two ways to process a pack:
//   1. Fold expressions (C++17) — collapse the pack over an operator in one line.
//   2. Recursive unpacking     — peel off one type at a time (pre-C++17 style).
//
// Prefer fold expressions when available; recursion is still useful when
// you need to do different things with each element.
// ─────────────────────────────────────────────────────────────────────


// ── Fold expressions ─────────────────────────────────────────────────
//
// (op ... pack)  — left fold:  ((a op b) op c) op d
// (pack op ...)  — right fold: a op (b op (c op d))

template<typename... Ts>
auto sum(Ts... args)
{
    return (args + ...);          // unary right fold over +
}

template<typename... Ts>
bool all_positive(Ts... args)
{
    return ((args > 0) && ...);  // fold over &&: short-circuits
}

template<typename... Ts>
void print_all(Ts&&... args)
{
    // Fold over comma operator using a lambda to add spacing.
    // The space before args ensures we don't print a leading space.
    std::size_t i = 0;
    ((std::cout << (i++ ? " " : "") << args), ...);
    std::cout << "\n";
}


// ── Recursive unpacking ───────────────────────────────────────────────
//
// Base case handles the last element; the recursive case peels one off.
// Needed when fold expressions can't express the logic (e.g. different
// behaviour for first/last element, or building data structures).

// Base case: single element.
template<typename T>
void print_typed(const T& t)
{
    std::cout << t << " [last]\n";
}

// Recursive case: print head, recurse on tail.
template<typename T, typename... Rest>
void print_typed(const T& head, const Rest&... rest)
{
    std::cout << head << " -> ";
    print_typed(rest...);
}


// ── Variadic class template: a minimal Tuple ─────────────────────────
//
// Built using inheritance to store each type at a different level.
// std::tuple in the standard library uses a similar approach.

// Empty base: zero elements.
template<typename... Ts>
struct Tuple {};

// Recursive case: store Head at this level, tail in the base.
template<typename Head, typename... Tail>
struct Tuple<Head, Tail...> : Tuple<Tail...>
{
    explicit Tuple(Head h, Tail... t)
        : Tuple<Tail...>(t...), head(h) {}

    Head head;
};

// get<N>(tuple): peel off N levels of inheritance.
template<std::size_t N, typename Head, typename... Tail>
auto& get(Tuple<Head, Tail...>& t)
{
    if constexpr (N == 0) return t.head;
    else                  return get<N - 1>(static_cast<Tuple<Tail...>&>(t));
}


// ── sizeof... ────────────────────────────────────────────────────────
template<typename... Ts>
void show_count(Ts...)
{
    std::cout << "pack has " << sizeof...(Ts) << " elements\n";
}


int main()
{
    // ── Fold expressions ─────────────────────────────────────────────
    std::cout << sum(1, 2, 3, 4, 5)           << "\n";  // 15
    std::cout << sum(1.5, 2.5, 3.0)           << "\n";  // 7.0
    std::cout << all_positive(1, 2, 3)         << "\n";  // 1
    std::cout << all_positive(1, -1, 3)        << "\n";  // 0

    print_all(10, "hello", 3.14, true);

    // ── Recursive unpacking ──────────────────────────────────────────
    std::cout << "\n";
    print_typed(1, "two", 3.0);

    // ── Variadic Tuple ───────────────────────────────────────────────
    std::cout << "\n";
    Tuple<int, std::string, double> t(42, "hello", 3.14);
    std::cout << get<0>(t) << "\n";  // 42
    std::cout << get<1>(t) << "\n";  // hello
    std::cout << get<2>(t) << "\n";  // 3.14

    // ── sizeof... ────────────────────────────────────────────────────
    std::cout << "\n";
    show_count(1, 2, 3);
    show_count('a');
    show_count();
}
