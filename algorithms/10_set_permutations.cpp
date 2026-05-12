#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Set operations and permutation/reordering algorithms.
//
// SET OPERATIONS — require both input ranges to be SORTED.
//   set_union             — all elements from either range (no duplicates).
//   set_intersection      — only elements present in BOTH ranges.
//   set_difference        — elements in range1 but NOT in range2.
//   set_symmetric_difference — elements in either but NOT both.
//   Output ranges must be pre-allocated or use back_inserter.
//
// PERMUTATIONS
//   next_permutation — advance to next lexicographic order; returns false
//                      when wrapping from last to first permutation.
//   prev_permutation — reverse direction.
//
// REORDERING
//   reverse(first, last)              — in-place reversal.
//   rotate(first, n_first, last)      — n_first becomes the new front.
//   shuffle(first, last, rng)         — random reorder.
//   copy / copy_if / move             — transfer elements to output range.
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── Set operations ────────────────────────────────────────────────
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {3, 4, 5, 6, 7};
    std::vector<int> out;

    std::set_union(a.begin(), a.end(), b.begin(), b.end(),
                   std::back_inserter(out));
    print(out, "union"); // 1 2 3 4 5 6 7
    out.clear();

    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::back_inserter(out));
    print(out, "intersection"); // 3 4 5
    out.clear();

    std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                        std::back_inserter(out));
    print(out, "a - b"); // 1 2
    out.clear();

    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(),
                                  std::back_inserter(out));
    print(out, "symmetric diff"); // 1 2 6 7

    // ── next_permutation ──────────────────────────────────────────────
    // Iterate over all permutations of {1,2,3} by starting from sorted order.
    std::vector<int> perm = {1, 2, 3};
    std::cout << "all permutations of {1,2,3}:\n";
    do {
        for (int x : perm) std::cout << x;
        std::cout << " ";
    } while (std::next_permutation(perm.begin(), perm.end()));
    std::cout << "\n"; // 123 132 213 231 312 321

    // ── reverse ───────────────────────────────────────────────────────
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::reverse(v.begin(), v.end());
    print(v, "reversed"); // 5 4 3 2 1

    // ── rotate ────────────────────────────────────────────────────────
    // rotate(first, n_first, last): element at n_first moves to front.
    std::vector<int> v2 = {1, 2, 3, 4, 5};
    std::rotate(v2.begin(), v2.begin() + 2, v2.end()); // bring index 2 to front
    print(v2, "rotate by 2"); // 3 4 5 1 2

    // ── copy_if ───────────────────────────────────────────────────────
    std::vector<int> src = {1, 2, 3, 4, 5, 6};
    std::vector<int> evens;
    std::copy_if(src.begin(), src.end(), std::back_inserter(evens),
                 [](int x){ return x % 2 == 0; });
    print(evens, "copy_if evens"); // 2 4 6

    // ── move (transfer ownership) ─────────────────────────────────────
    std::vector<std::string> words = {"hello", "world", "foo"};
    std::vector<std::string> dest(words.size());
    std::move(words.begin(), words.end(), dest.begin());
    // words elements are now in a valid but unspecified state
    std::cout << "moved dest[0]=" << dest[0] << "\n"; // hello
}
