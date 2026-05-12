#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Min/max and heap algorithms.
//
// min_element / max_element / minmax_element — O(N) scans.
// clamp(v, lo, hi)  — returns lo if v<lo, hi if v>hi, else v (C++17).
//
// A heap is a range where front() is always the maximum (max-heap by default).
// Heap operations do NOT sort — they only maintain the heap invariant.
//
// make_heap   — turn a range into a heap in O(N).
// push_heap   — after push_back, extend the heap in O(log N).
// pop_heap    — move the max to back() in O(log N); call pop_back() after.
// sort_heap   — convert heap → ascending sorted range; destroys heap.
// is_heap     — check heap invariant.
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── min_element / max_element ─────────────────────────────────────
    std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};
    auto mn = std::min_element(v.begin(), v.end());
    auto mx = std::max_element(v.begin(), v.end());
    std::cout << "min=" << *mn << " max=" << *mx << "\n"; // 1  9

    // ── minmax_element — both in one pass ─────────────────────────────
    auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    std::cout << "minmax: " << *lo << " " << *hi << "\n"; // 1  9

    // With a comparator (find by absolute value):
    std::vector<int> v2 = {-5, 3, -1, 4, -9};
    auto abs_max = std::max_element(v2.begin(), v2.end(),
                       [](int a, int b){ return std::abs(a) < std::abs(b); });
    std::cout << "largest abs: " << *abs_max << "\n"; // -9

    // ── clamp ─────────────────────────────────────────────────────────
    std::cout << std::clamp(3,  0, 10) << "\n"; // 3   (within range)
    std::cout << std::clamp(-2, 0, 10) << "\n"; // 0   (below lo)
    std::cout << std::clamp(15, 0, 10) << "\n"; // 10  (above hi)

    // ── Heap ──────────────────────────────────────────────────────────
    std::vector<int> h = {3, 1, 4, 1, 5, 9, 2};
    std::make_heap(h.begin(), h.end()); // h.front() == 9
    std::cout << "heap top: " << h.front() << "\n"; // 9
    print(h, "after make_heap");

    // push: push_back first, then push_heap to maintain invariant.
    h.push_back(7);
    std::push_heap(h.begin(), h.end());
    std::cout << "after push 7, top: " << h.front() << "\n"; // 9

    // pop: pop_heap moves max to back, then pop_back removes it.
    std::pop_heap(h.begin(), h.end());
    std::cout << "popped: " << h.back() << "\n"; // 9
    h.pop_back();
    std::cout << "new top: " << h.front() << "\n"; // 7

    // sort_heap: heap → ascending sorted. Destroys the heap property.
    std::make_heap(h.begin(), h.end());
    std::sort_heap(h.begin(), h.end());
    print(h, "sort_heap"); // ascending
}
