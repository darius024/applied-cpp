#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Linear search algorithms — O(N), work on unsorted ranges.
//
// find(first, last, value)           — first element equal to value.
// find_if(first, last, pred)         — first element where pred(e) is true.
// find_if_not(first, last, pred)     — first element where pred(e) is false.
// find_first_of(f1,l1, f2,l2)       — first element in range1 found in range2.
// search(f1,l1, f2,l2)              — first occurrence of range2 as sub-sequence in range1.
// adjacent_find(first, last)         — first pair of consecutive equal elements.
//
// All return end() on failure. Always check before dereferencing.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};

    // ── find ──────────────────────────────────────────────────────────
    auto it = std::find(v.begin(), v.end(), 9);
    if (it != v.end())
        std::cout << "found 9 at index " << std::distance(v.begin(), it) << "\n"; // 5

    // ── find_if ───────────────────────────────────────────────────────
    auto even = std::find_if(v.begin(), v.end(), [](int x){ return x % 2 == 0; });
    std::cout << "first even: " << *even << "\n"; // 4

    // ── find_if_not ───────────────────────────────────────────────────
    auto not_one = std::find_if_not(v.begin(), v.end(), [](int x){ return x == 1; });
    std::cout << "first non-1: " << *not_one << "\n"; // 3

    // ── find_first_of ─────────────────────────────────────────────────
    // Find the first element in v that is in the target set.
    std::vector<int> targets = {9, 5};
    auto ffo = std::find_first_of(v.begin(), v.end(), targets.begin(), targets.end());
    std::cout << "first element that is 9 or 5: " << *ffo
              << " at index " << std::distance(v.begin(), ffo) << "\n"; // 5 at index 4

    // ── search ────────────────────────────────────────────────────────
    // Find sub-sequence {1, 5} inside v.
    std::vector<int> needle = {1, 5};
    auto pos = std::search(v.begin(), v.end(), needle.begin(), needle.end());
    if (pos != v.end())
        std::cout << "sub-sequence {1,5} at index "
                  << std::distance(v.begin(), pos) << "\n"; // 3

    // ── adjacent_find ─────────────────────────────────────────────────
    std::vector<int> v2 = {1, 2, 3, 3, 4, 5, 5};
    auto adj = std::adjacent_find(v2.begin(), v2.end());
    std::cout << "first consecutive pair: " << *adj << "\n"; // 3

    // With a predicate: first pair where second is double the first.
    auto adj2 = std::adjacent_find(v2.begin(), v2.end(),
                    [](int a, int b){ return b == a * 2; });
    // 1,2 → b==a*2: true
    std::cout << "first pair where b==2a: " << *adj2 << "\n"; // 1

    // ── String search ─────────────────────────────────────────────────
    std::string text = "the quick brown fox";
    std::string word = "brown";
    auto sit = std::search(text.begin(), text.end(), word.begin(), word.end());
    if (sit != text.end())
        std::cout << "found \"brown\" at pos "
                  << std::distance(text.begin(), sit) << "\n"; // 10
}
