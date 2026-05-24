#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Sorting algorithms.
//
// sort          — O(N log N) worst case (since C++11), not stable.
// stable_sort   — O(N log N) if extra memory is available,
//                 O(N log² N) otherwise; preserves order of equal elements.
// partial_sort  — O(N log M): only the first M positions are sorted correctly.
// nth_element   — O(N) average: element at position n is the one that would
//                 be there in a sorted range; rest are unordered.
//
// All accept an optional comparator: any callable (a,b)->bool satisfying
// strict weak ordering. Default is operator< (ascending).
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

struct Person { std::string name; int age; };

int main()
{
    // ── sort ──────────────────────────────────────────────────────────
    std::vector<int> v = {5, 3, 8, 1, 9, 2, 7, 4, 6};
    std::sort(v.begin(), v.end());
    print(v, "sort asc");

    std::sort(v.begin(), v.end(), std::greater<int>{});
    print(v, "sort desc");

    // ── Custom comparator ─────────────────────────────────────────────
    std::vector<Person> people = {{"alice", 30}, {"bob", 25}, {"carol", 30}, {"dave", 25}};
    // Sort by age ascending; within the same age sort by name.
    std::sort(people.begin(), people.end(), [](const Person& a, const Person& b){
        if (a.age != b.age) return a.age < b.age;
        return a.name < b.name;
    });
    for (auto& p : people) std::cout << p.name << "(" << p.age << ") ";
    std::cout << "\n"; // bob(25) dave(25) alice(30) carol(30)

    // ── stable_sort ───────────────────────────────────────────────────
    // Same as above but "equal" elements preserve insertion order.
    std::vector<Person> p2 = {{"alice", 30}, {"bob", 25}, {"carol", 30}, {"dave", 25}};
    std::stable_sort(p2.begin(), p2.end(), [](const Person& a, const Person& b){
        return a.age < b.age;
    });
    // bob and dave stay in original relative order; same for alice and carol.
    for (auto& p : p2) std::cout << p.name << "(" << p.age << ") ";
    std::cout << "\n"; // bob(25) dave(25) alice(30) carol(30)

    // ── partial_sort ──────────────────────────────────────────────────
    // Sort only the first M=3 positions. The rest are unspecified.
    std::vector<int> v2 = {5, 3, 8, 1, 9, 2, 7};
    std::partial_sort(v2.begin(), v2.begin() + 3, v2.end());
    print(v2, "partial_sort(3)"); // 1 2 3 | rest unspecified

    // ── nth_element ───────────────────────────────────────────────────
    // Rearranges so v[n] is the element that would be at position n in a
    // sorted range. Elements before n are ≤ v[n]; elements after are ≥ v[n].
    // Useful for median / top-K without a full sort.
    std::vector<int> v3 = {5, 3, 8, 1, 9, 2, 7, 4, 6};
    auto mid = v3.begin() + v3.size() / 2;
    std::nth_element(v3.begin(), mid, v3.end());
    std::cout << "median candidate: " << *mid << "\n"; // 5
}
