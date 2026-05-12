#include <iostream>
#include <algorithm>
#include <ranges>
#include <vector>
#include <string>
#include <numeric>

// ─────────────────────────────────────────────────────────────────────
// std::ranges (C++20): constrained, composable algorithm and view library.
//
// Two parts:
//   1. std::ranges:: algorithms — accept containers directly, support
//      projections, produce clearer error messages.
//   2. std::views:: adaptors — lazy, composable range pipelines.
//      Elements are computed on demand; no intermediate allocations.
//
// Projection: a callable applied to each element BEFORE comparison.
//   ranges::sort(v, {}, &Person::age)  — sort by .age without a lambda.
//   {} is the default comparator (std::ranges::less).
// ─────────────────────────────────────────────────────────────────────

struct Person { std::string name; int age; };

void print_ints(auto&& rng, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (auto x : rng) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── ranges:: algorithms: accept containers directly ───────────────
    std::vector<int> v = {5, 3, 8, 1, 9, 2, 7};
    std::ranges::sort(v);
    print_ints(v, "ranges::sort"); // 1 2 3 5 7 8 9

    auto it = std::ranges::find(v, 7);
    std::cout << "find 7: " << *it << "\n"; // 7

    // ── Projection: sort by field without writing a comparator lambda ──
    std::vector<Person> people = {{"carol", 30}, {"alice", 25}, {"bob", 35}};
    std::ranges::sort(people, {}, &Person::age); // sort by .age ascending
    for (auto& p : people) std::cout << p.name << "(" << p.age << ") ";
    std::cout << "\n"; // alice(25) carol(30) bob(35)

    // ── views:: pipeline: lazy, composable ────────────────────────────
    // No intermediate vectors — each element flows through the pipeline
    // only when the final range is iterated.

    std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // filter → transform → take
    auto pipeline = nums
        | std::views::filter   ([](int x){ return x % 2 == 0; }) // 2 4 6 8 10
        | std::views::transform([](int x){ return x * x; })       // 4 16 36 64 100
        | std::views::take(3);                                     // 4 16 36
    print_ints(pipeline, "filter|transform|take(3)");

    // ── views::iota — generate a sequence lazily ──────────────────────
    print_ints(std::views::iota(1, 6), "iota(1,6)"); // 1 2 3 4 5

    // Combine iota with transform (squares of 1..5):
    auto squares = std::views::iota(1, 6)
                 | std::views::transform([](int x){ return x * x; });
    print_ints(squares, "squares"); // 1 4 9 16 25

    // ── views::reverse ────────────────────────────────────────────────
    print_ints(v | std::views::reverse, "reverse"); // 9 8 7 5 3 2 1

    // ── views::drop / views::take ─────────────────────────────────────
    print_ints(v | std::views::drop(2) | std::views::take(3), "drop(2)|take(3)"); // 3 5 7

    // ── Materialise a view into a vector (C++23: std::ranges::to) ─────
    // In C++20 use a manual copy:
    std::vector<int> result;
    for (int x : squares) result.push_back(x);
    print_ints(result, "materialised squares"); // 1 4 9 16 25

    // ── ranges:: algorithms with projections ──────────────────────────
    auto oldest = std::ranges::max_element(people, {}, &Person::age);
    std::cout << "oldest: " << oldest->name << "\n"; // bob

    std::cout << std::boolalpha;
    bool any_young = std::ranges::any_of(people, [](int a){ return a < 30; },
                                         &Person::age);
    std::cout << "any under 30: " << any_young << "\n"; // true
}
