#include <iostream>
#include <concepts>
#include <ranges>
#include <vector>
#include <list>
#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────
// Standard library concepts (C++20).
//
// <concepts>  — type relationships and basic properties
// <iterator>  — iterator category concepts
// <ranges>    — range concepts
//
// Using standard concepts as building blocks for your own is the
// idiomatic C++20 approach — composing is better than reimplementing.
// ─────────────────────────────────────────────────────────────────────


// ── <concepts>: type relationship concepts ────────────────────────────

template<typename T, typename U>
void show_same(T, U)
{
    std::cout << "same_as<T,U>:        " << std::same_as<T, U>        << "\n";
    std::cout << "convertible_to<T,U>: " << std::convertible_to<T, U> << "\n";
}

// ── <concepts>: callable concepts ────────────────────────────────────
//
// std::invocable<F, Args...>: F can be called with Args...
// std::predicate<F, Args...>: F is invocable and returns bool

template<typename F, typename T>
    requires std::predicate<F, T>
std::vector<T> filter(const std::vector<T>& v, F pred)
{
    std::vector<T> result;
    for (const auto& x : v)
        if (pred(x)) result.push_back(x);
    return result;
}

// ── <ranges>: range concepts ──────────────────────────────────────────
//
// std::ranges::range<R>:       R has begin/end
// std::ranges::sized_range<R>: R also has size()
// std::ranges::input_range<R>: R's iterator satisfies InputIterator

template<std::ranges::range R>
void print_range(const R& r)
{
    for (const auto& x : r) std::cout << x << " ";
    std::cout << "\n";
}

template<std::ranges::sized_range R>
void print_with_size(const R& r)
{
    std::cout << "size=" << std::ranges::size(r) << ": ";
    print_range(r);
}

// ── Building custom concepts from standard ones ───────────────────────
//
// Compose with && and || — no boilerplate, readable constraints.

// A type that is sortable: random-access range whose elements are totally ordered.
template<typename R>
concept SortableRange =
    std::ranges::random_access_range<R> &&
    std::sortable<std::ranges::iterator_t<R>>;

template<SortableRange R>
void sort_and_print(R& r)
{
    std::ranges::sort(r);
    print_range(r);
}

// A numeric range: range of arithmetic elements.
template<typename R>
concept NumericRange =
    std::ranges::input_range<R> &&
    std::integral<std::ranges::range_value_t<R>>;

template<NumericRange R>
auto range_sum(const R& r)
{
    std::ranges::range_value_t<R> acc{};
    for (auto x : r) acc += x;
    return acc;
}


int main()
{
    std::cout << std::boolalpha;

    // ── type relationships ────────────────────────────────────────────
    show_same(1, 1);        // same_as=true
    std::cout << "\n";
    show_same(1, 1.0);      // same_as=false, convertible=true
    std::cout << "\n";

    // ── invocable / predicate ─────────────────────────────────────────
    std::vector<int> v = {1, -2, 3, -4, 5};
    auto pos = filter(v, [](int x){ return x > 0; });
    print_range(pos);  // 1 3 5

    // ── range concepts ────────────────────────────────────────────────
    std::vector<std::string> words = {"hello", "world", "cpp"};
    print_with_size(words);

    std::list<int> lst = {10, 20, 30};  // range but NOT sized_range
    print_range(lst);
    // print_with_size(lst); // compile error: list has no O(1) size()

    // ── SortableRange ─────────────────────────────────────────────────
    std::vector<int> nums = {5, 2, 8, 1, 9};
    sort_and_print(nums);

    // ── NumericRange ──────────────────────────────────────────────────
    std::cout << "sum = " << range_sum(nums) << "\n";
}
