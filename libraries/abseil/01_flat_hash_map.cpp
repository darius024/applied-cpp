#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/hash/hash.h>
#include <iostream>
#include <string>
#include <string_view>

// compile: g++ -std=c++17 01_flat_hash_map.cpp -o 01_flat_hash_map \
//          -labsl_hash -labsl_raw_hash_set -labsl_base -labsl_city \
//          -labsl_low_level_hash -labsl_strings -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// absl::flat_hash_map / flat_hash_set
//
// Open-addressing table with a "Swiss table" layout:
//   - A parallel array of 1-byte "control" values holds a 7-bit hash
//     fingerprint per slot (or a sentinel: empty / deleted / end).
//   - SSE2/NEON loads 16 control bytes at once and compares them to
//     the probe fingerprint in a single instruction.
//   - A mismatch in the fingerprint skips the full key comparison,
//     so most probes are resolved in one cacheline load.
//
// vs std::unordered_map:
//   - No per-element heap allocation (open-addressing, not chained).
//   - Better cache utilisation: keys and values are stored inline.
//   - Heterogeneous lookup: find(string_view) on map<string,…> avoids
//     constructing a std::string key.
//
// Pointer/iterator stability: NOT guaranteed on insert/rehash.
//   Use node_hash_map if you need stable pointers.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Basic map operations ──────────────────────────────────────────
    absl::flat_hash_map<int, double> scores;
    scores.reserve(64);

    scores[1] = 9.5;
    scores.emplace(2, 8.1);
    scores.insert_or_assign(3, 7.7);

    std::cout << "score[2]: " << scores.at(2) << "\n";

    if (auto it = scores.find(3); it != scores.end())
        std::cout << "found 3 → " << it->second << "\n";

    // ── Heterogeneous lookup (no string construction) ─────────────────
    absl::flat_hash_map<std::string, int> word_count;
    word_count["hello"]++;
    word_count["world"]++;
    word_count["hello"]++;

    // Pass string_view — no std::string allocation on lookup
    std::string_view key = "hello";
    auto it = word_count.find(key);
    if (it != word_count.end())
        std::cout << "hello count: " << it->second << "\n"; // 2

    // ── flat_hash_set ─────────────────────────────────────────────────
    absl::flat_hash_set<int> seen;
    for (int x : {3, 1, 4, 1, 5, 9, 2, 6, 5, 3})
        seen.insert(x);
    std::cout << "unique: " << seen.size() << "\n"; // 7

    std::cout << "contains 5: " << std::boolalpha << seen.contains(5) << "\n";
    std::cout << "contains 7: " << seen.contains(7) << "\n";

    // ── Iteration ─────────────────────────────────────────────────────
    // Order is NOT guaranteed (hash map, not sorted).
    std::cout << "scores: ";
    for (const auto& [k, v] : scores)
        std::cout << k << ":" << v << " ";
    std::cout << "\n";

    // ── erase ─────────────────────────────────────────────────────────
    scores.erase(2);
    std::cout << "after erase(2) size: " << scores.size() << "\n"; // 2
}
