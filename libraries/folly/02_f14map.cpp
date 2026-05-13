#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 02_f14map.cpp -o 02_f14map -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::F14FastMap / F14VectorMap / F14NodeMap / F14ValueMap
//
// All use SIMD (SSE2 on x86, NEON on ARM) to probe 14 slots per
// cacheline in a single instruction, giving 2-4x throughput over
// std::unordered_map for integer and short-string keys.
//
// Variants:
//   F14FastMap    — stores KV pairs in the hash table itself.
//                   Best overall performance; pointers invalidate on rehash.
//   F14VectorMap  — stores KV pairs in a separate std::vector.
//                   Pointer/iterator stability across inserts; good for
//                   large values where cache locality matters.
//   F14NodeMap    — heap-allocates each node; pointer-stable like
//                   std::unordered_map. Slowest of the four.
//   F14ValueMap   — alias for F14FastMap (policy-based spelling).
//
// F14FastSet / F14VectorSet are the set equivalents.
//
// API is identical to std::unordered_map — just change the type.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── F14FastMap: integer keys ──────────────────────────────────────
    folly::F14FastMap<int, double> scores;
    scores.reserve(64); // pre-size to avoid any rehash

    scores[1] = 9.5;
    scores[2] = 8.1;
    scores.emplace(3, 7.7);

    std::cout << "score[2]: " << scores.at(2) << "\n";
    std::cout << "size: "     << scores.size() << "\n";

    // find returns iterator, same as std::unordered_map
    if (auto it = scores.find(3); it != scores.end())
        std::cout << "found 3 → " << it->second << "\n";

    // ── F14FastMap: string keys ───────────────────────────────────────
    folly::F14FastMap<std::string, int> word_count;
    for (const char* w : {"the", "cat", "sat", "on", "the", "mat", "the"})
        ++word_count[w];

    std::cout << "the: " << word_count["the"] << "\n"; // 3

    // ── F14VectorMap: pointer-stable values ──────────────────────────
    // Useful when you hold raw pointers/references into the map's values.
    folly::F14VectorMap<int, std::string> labels;
    labels[1] = "worker-1";
    labels[2] = "worker-2";

    const std::string& ref = labels.at(1); // stable across more inserts
    labels[3] = "worker-3";
    labels[4] = "worker-4";
    std::cout << "stable ref still valid: " << ref << "\n"; // worker-1

    // ── F14FastSet ────────────────────────────────────────────────────
    folly::F14FastSet<int> seen;
    for (int x : {5, 3, 5, 1, 3, 7}) seen.insert(x);
    std::cout << "unique count: " << seen.size() << "\n"; // 4

    // ── Iteration (same as std::unordered_map) ────────────────────────
    std::cout << "scores: ";
    for (const auto& [k, v] : scores)
        std::cout << k << ":" << v << " ";
    std::cout << "\n";
}
