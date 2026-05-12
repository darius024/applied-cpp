#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// remove / remove_if / unique — they do NOT shrink the container.
//
// They move "kept" elements to the front and return an iterator `new_end`
// pointing to the new logical end. Elements in [new_end, old_end) are
// valid but unspecified — the container still has its original size.
//
// You must follow with .erase() to actually shrink:
//   v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
//   ↑ the "erase-remove idiom"
//
// C++20: std::erase(v, val) and std::erase_if(v, pred) do both in one call.
//
// unique: removes *consecutive* duplicates.
//   Sort the range first if you want ALL duplicates removed.
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── remove: erase-remove idiom ────────────────────────────────────
    std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};
    v.erase(std::remove(v.begin(), v.end(), 2), v.end());
    print(v, "remove 2"); // 1 3 4 5

    // ── remove_if ─────────────────────────────────────────────────────
    std::vector<int> v2 = {1, 2, 3, 4, 5, 6, 7, 8};
    v2.erase(std::remove_if(v2.begin(), v2.end(),
             [](int x){ return x % 2 == 0; }), v2.end());
    print(v2, "remove evens"); // 1 3 5 7

    // ── C++20: std::erase_if ──────────────────────────────────────────
    std::vector<int> v3 = {1, 2, 3, 4, 5, 6};
    std::erase_if(v3, [](int x){ return x > 3; });
    print(v3, "erase_if >3"); // 1 2 3

    std::vector<int> v4 = {1, 2, 3, 2, 1};
    std::erase(v4, 2); // erase all elements equal to 2
    print(v4, "erase 2"); // 1 3 1

    // ── unique: remove consecutive duplicates ─────────────────────────
    std::vector<int> v5 = {1, 1, 2, 3, 3, 3, 4, 4, 5};
    v5.erase(std::unique(v5.begin(), v5.end()), v5.end());
    print(v5, "unique (already sorted)"); // 1 2 3 4 5

    // unique on unsorted input only removes ADJACENT duplicates:
    std::vector<int> v6 = {3, 1, 1, 2, 3, 2};
    v6.erase(std::unique(v6.begin(), v6.end()), v6.end());
    print(v6, "unique (unsorted)"); // 3 1 2 3 2 — not fully deduped

    // To remove ALL duplicates: sort first, then unique+erase.
    std::vector<int> v7 = {3, 1, 1, 2, 3, 2};
    std::sort(v7.begin(), v7.end());
    v7.erase(std::unique(v7.begin(), v7.end()), v7.end());
    print(v7, "sort+unique"); // 1 2 3
}
