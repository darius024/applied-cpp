#include <absl/container/btree_map.h>
#include <absl/container/btree_set.h>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 02_btree.cpp -o 02_btree \
//          -labsl_base -labsl_strings -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// absl::btree_map / btree_set
//
// Drop-in replacements for std::map / std::set backed by a B-tree
// instead of a red-black tree. API is identical; performance is not.
//
// Why B-tree beats std::map:
//   - Each node stores multiple keys (typically 4–16 depending on T).
//   - A traversal touches far fewer nodes → far fewer cache misses.
//   - Memory overhead is 4–16× lower: one allocation per node
//     instead of one allocation per element.
//   - Range iteration is significantly faster (sequential memory).
//
// Trade-offs:
//   - Insert/erase are slightly slower when nodes must be split/merged.
//   - Pointer/iterator stability NOT guaranteed (like flat_hash_map).
//   - Equal performance to std::map for point lookups on small maps.
//
// btree_multimap / btree_multiset are also available.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── btree_map: sorted key-value store ─────────────────────────────
    absl::btree_map<int, std::string> bm;
    bm[5] = "five";
    bm[1] = "one";
    bm[3] = "three";
    bm[7] = "seven";
    bm[2] = "two";

    // Iteration is always in sorted key order (same as std::map)
    std::cout << "btree_map (sorted):\n";
    for (const auto& [k, v] : bm)
        std::cout << "  " << k << " → " << v << "\n";

    // lower_bound / upper_bound — same as std::map
    auto lo = bm.lower_bound(3);
    auto hi = bm.upper_bound(5);
    std::cout << "range [3, 5]: ";
    for (auto it = lo; it != hi; ++it)
        std::cout << it->first << " ";
    std::cout << "\n"; // 3 5

    // ── btree_set ─────────────────────────────────────────────────────
    absl::btree_set<int> bs{9, 3, 7, 1, 5};

    std::cout << "btree_set: ";
    for (int x : bs) std::cout << x << " "; // 1 3 5 7 9
    std::cout << "\n";

    std::cout << "contains 5: " << std::boolalpha << bs.contains(5) << "\n";
    bs.erase(3);
    std::cout << "after erase(3): ";
    for (int x : bs) std::cout << x << " "; // 1 5 7 9
    std::cout << "\n";

    // ── btree_multimap ────────────────────────────────────────────────
    absl::btree_multimap<std::string, int> events;
    events.emplace("click", 1);
    events.emplace("click", 2);
    events.emplace("hover", 1);

    auto [first, last] = events.equal_range("click");
    std::cout << "click events: ";
    for (auto it = first; it != last; ++it)
        std::cout << it->second << " "; // 1 2
    std::cout << "\n";
}
