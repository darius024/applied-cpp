#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Binary search algorithms — O(log N), require a SORTED range.
// Using them on an unsorted range is undefined behaviour.
//
// binary_search(first, last, v)  — bool: is v present?
// lower_bound(first, last, v)    — iterator to first element NOT LESS than v.
// upper_bound(first, last, v)    — iterator to first element GREATER than v.
// equal_range(first, last, v)    — pair{lower_bound, upper_bound}: the
//                                  sub-range of elements equal to v.
//
// All accept an optional comparator matching the one used to sort.
// ─────────────────────────────────────────────────────────────────────

void print_range(const std::string& label,
                 std::vector<int>::const_iterator first,
                 std::vector<int>::const_iterator last)
{
    std::cout << label << ": [";
    for (auto it = first; it != last; ++it)
        std::cout << (it != first ? ", " : "") << *it;
    std::cout << "]\n";
}

int main()
{
    std::vector<int> v = {1, 2, 4, 4, 4, 6, 8, 10};

    // ── binary_search ─────────────────────────────────────────────────
    std::cout << std::boolalpha;
    std::cout << "binary_search(4): " << std::binary_search(v.begin(), v.end(), 4) << "\n"; // true
    std::cout << "binary_search(5): " << std::binary_search(v.begin(), v.end(), 5) << "\n"; // false

    // ── lower_bound ───────────────────────────────────────────────────
    // Points to the first 4. Use for insertion to keep sorted order.
    auto lo = std::lower_bound(v.begin(), v.end(), 4);
    std::cout << "lower_bound(4) index: " << std::distance(v.begin(), lo) << "\n"; // 2

    // lower_bound on a missing value → first element greater than it.
    auto lo5 = std::lower_bound(v.begin(), v.end(), 5);
    std::cout << "lower_bound(5) points to: " << *lo5 << "\n"; // 6

    // ── upper_bound ───────────────────────────────────────────────────
    // Points one past the last 4.
    auto hi = std::upper_bound(v.begin(), v.end(), 4);
    std::cout << "upper_bound(4) index: " << std::distance(v.begin(), hi) << "\n"; // 5

    // ── equal_range ───────────────────────────────────────────────────
    // Both bounds at once — the half-open range of elements equal to 4.
    auto [first, last] = std::equal_range(v.begin(), v.end(), 4);
    std::cout << "equal_range(4) count: " << std::distance(first, last) << "\n"; // 3
    print_range("equal_range(4)", first, last); // [4, 4, 4]

    // equal_range on a missing value → first == last (empty range)
    auto [f5, l5] = std::equal_range(v.begin(), v.end(), 5);
    std::cout << "equal_range(5) empty: " << (f5 == l5) << "\n"; // true

    // ── Sorted insertion maintaining order ────────────────────────────
    std::vector<int> sorted = {1, 3, 5, 7};
    int val = 4;
    sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), val), val);
    for (int x : sorted) std::cout << x << " "; // 1 3 4 5 7
    std::cout << "\n";

    // ── Custom comparator ─────────────────────────────────────────────
    // Range sorted descending — comparator must match.
    std::vector<int> desc = {10, 8, 6, 4, 2};
    auto it = std::lower_bound(desc.begin(), desc.end(), 5, std::greater<int>{});
    std::cout << "lower_bound(5, desc) points to: " << *it << "\n"; // 4
}
