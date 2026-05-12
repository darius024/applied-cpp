#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Predicate and counting algorithms.
//
// any_of (first, last, pred)     — true if pred returns true for at least one element.
// all_of (first, last, pred)     — true if pred returns true for ALL elements.
// none_of(first, last, pred)     — true if pred returns true for NO elements.
//   All short-circuit: they stop at the first decisive element.
//   All return true on an empty range (vacuous truth) — except any_of → false.
//
// count   (first, last, val)     — number of elements equal to val.
// count_if(first, last, pred)    — number of elements satisfying pred.
//
// equal   (f1,l1, f2)            — true if both ranges are pairwise equal.
// mismatch(f1,l1, f2)            — pair of iterators to the first differing positions.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    std::vector<int> v = {2, 4, 6, 8, 10};
    std::vector<int> mixed = {2, 4, 5, 8, 10};

    std::cout << std::boolalpha;

    // ── any_of / all_of / none_of ─────────────────────────────────────
    auto is_even = [](int x){ return x % 2 == 0; };
    auto is_neg  = [](int x){ return x < 0; };

    std::cout << "all even:  " << std::all_of (v.begin(), v.end(), is_even) << "\n"; // true
    std::cout << "any neg:   " << std::any_of (v.begin(), v.end(), is_neg)  << "\n"; // false
    std::cout << "none neg:  " << std::none_of(v.begin(), v.end(), is_neg)  << "\n"; // true

    std::cout << "mixed all even: " << std::all_of(mixed.begin(), mixed.end(), is_even) << "\n"; // false
    std::cout << "mixed any odd:  " << std::any_of(mixed.begin(), mixed.end(),
                                         [](int x){ return x % 2 != 0; }) << "\n"; // true

    // Empty range: all_of → true (vacuously), any_of → false, none_of → true
    std::vector<int> empty;
    std::cout << "empty all_of:  " << std::all_of (empty.begin(), empty.end(), is_even) << "\n"; // true
    std::cout << "empty any_of:  " << std::any_of (empty.begin(), empty.end(), is_even) << "\n"; // false
    std::cout << "empty none_of: " << std::none_of(empty.begin(), empty.end(), is_even) << "\n"; // true

    // ── count / count_if ─────────────────────────────────────────────
    std::vector<int> nums = {1, 2, 2, 3, 2, 4, 5};
    std::cout << "count 2:     " << std::count   (nums.begin(), nums.end(), 2) << "\n"; // 3
    std::cout << "count >2:    " << std::count_if(nums.begin(), nums.end(),
                                      [](int x){ return x > 2; }) << "\n"; // 3

    // ── equal ─────────────────────────────────────────────────────────
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {1, 2, 3};
    std::vector<int> c = {1, 2, 4};
    std::cout << "a==b: " << std::equal(a.begin(), a.end(), b.begin()) << "\n"; // true
    std::cout << "a==c: " << std::equal(a.begin(), a.end(), c.begin()) << "\n"; // false

    // ── mismatch ──────────────────────────────────────────────────────
    auto [it1, it2] = std::mismatch(a.begin(), a.end(), c.begin());
    if (it1 != a.end())
        std::cout << "first mismatch: a=" << *it1 << " c=" << *it2 << "\n"; // a=3 c=4
}
