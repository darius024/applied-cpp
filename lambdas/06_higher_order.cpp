#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <numeric>
#include <algorithm>
#include <optional>

// ─────────────────────────────────────────────────────────────────────
// Higher-order functions — functions that take or return functions.
//
// The canonical trio: map, filter, reduce (fold).
// These replace hand-written loops with composable, named operations
// that express WHAT is done, not HOW.
//
// In C++ the stdlib already has transform, copy_if, accumulate.
// Here we implement lightweight versions for clarity, then show how
// to compose them and how returning lambdas works.
// ─────────────────────────────────────────────────────────────────────


// ── map: transform each element ──────────────────────────────────────
template<typename T, typename F>
auto map(const std::vector<T>& v, F f)
{
    using R = decltype(f(v[0]));
    std::vector<R> out;
    out.reserve(v.size());
    for (const auto& x : v) out.push_back(f(x));
    return out;
}

// ── filter: keep elements satisfying a predicate ─────────────────────
template<typename T, typename F>
std::vector<T> filter(const std::vector<T>& v, F pred)
{
    std::vector<T> out;
    for (const auto& x : v) if (pred(x)) out.push_back(x);
    return out;
}

// ── reduce (fold): collapse to a single value ─────────────────────────
template<typename T, typename U, typename F>
U reduce(const std::vector<T>& v, U init, F op)
{
    for (const auto& x : v) init = op(std::move(init), x);
    return init;
}

// ── Returning a lambda: function factories ────────────────────────────
// A function that returns a lambda is a factory for behaviour.
// The returned lambda captures its arguments by value — safe to outlive
// the factory call.

auto make_multiplier(int factor)
{
    return [factor](int x){ return x * factor; };
}

auto make_between(int lo, int hi)
{
    return [lo, hi](int x){ return x >= lo && x <= hi; };
}

// ── Function composition ─────────────────────────────────────────────
// compose(f, g)(x) = f(g(x))
// The lambdas are captured by value — safe and cheap for small closures.

template<typename F, typename G>
auto compose(F f, G g)
{
    return [f, g](auto x){ return f(g(x)); };
}

// ── Partial application ───────────────────────────────────────────────
// Bind the first argument, return a unary function.
template<typename F, typename T>
auto partial(F f, T first)
{
    return [f, first](auto second){ return f(first, second); };
}


// ── Printing helpers ──────────────────────────────────────────────────
template<typename T>
void print_vec(const std::vector<T>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (const auto& x : v) std::cout << x << " ";
    std::cout << "\n";
}


int main()
{
    std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // ── map ───────────────────────────────────────────────────────────
    auto doubled  = map(nums, [](int x){ return x * 2; });
    auto as_str   = map(nums, [](int x){ return std::to_string(x); });
    print_vec(doubled, "doubled");
    print_vec(as_str,  "as_str");

    // ── filter ────────────────────────────────────────────────────────
    auto evens = filter(nums, [](int x){ return x % 2 == 0; });
    print_vec(evens, "evens");

    // ── reduce ────────────────────────────────────────────────────────
    int  sum  = reduce(nums, 0, [](int acc, int x){ return acc + x; });
    int  prod = reduce(nums, 1, [](int acc, int x){ return acc * x; });
    std::cout << "sum="  << sum  << "\n";
    std::cout << "prod=" << prod << "\n";

    // ── Chain: filter → map → reduce ─────────────────────────────────
    // Sum of squares of even numbers — no temp loop variables.
    int result = reduce(
        map(
            filter(nums, [](int x){ return x % 2 == 0; }),
            [](int x){ return x * x; }
        ),
        0,
        [](int acc, int x){ return acc + x; }
    );
    std::cout << "sum of squares of evens=" << result << "\n"; // 220

    // ── Function factories ────────────────────────────────────────────
    auto triple = make_multiplier(3);
    auto tripled = map(nums, triple);
    print_vec(tripled, "tripled");

    auto mid = filter(nums, make_between(4, 7));
    print_vec(mid, "between 4-7");

    // ── Composition ───────────────────────────────────────────────────
    auto double_then_add1 = compose([](int x){ return x + 1; },
                                    [](int x){ return x * 2; });
    std::cout << "double_then_add1(5)=" << double_then_add1(5) << "\n"; // 11

    // ── Partial application ───────────────────────────────────────────
    auto add = [](int a, int b){ return a + b; };
    auto add10 = partial(add, 10);
    auto added = map(evens, add10);
    print_vec(added, "evens + 10");
}
