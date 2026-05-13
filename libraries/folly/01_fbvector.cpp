#include <folly/FBVector.h>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 01_fbvector.cpp -o 01_fbvector -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::fbvector<T>: a drop-in replacement for std::vector.
//
// Key differences vs std::vector:
//   1. Cooperates with jemalloc: uses xallocx() to try to extend the
//      current allocation in-place. If successful, no elements are
//      copied — a pure O(1) grow. Falls back to the normal allocate-
//      copy-free path if the block cannot be extended.
//   2. Growth factor is 1.5x (vs 2x for most stdlib impls), reducing
//      peak memory usage for large vectors.
//   3. Uses __builtin_expect branch hints (FOLLY_LIKELY) throughout.
//
// API is identical to std::vector — just change the type.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Basic usage (identical to std::vector) ────────────────────────
    folly::fbvector<int> v;
    v.reserve(8);

    for (int i = 0; i < 10; ++i) v.push_back(i);

    std::cout << "size: "     << v.size()     << "\n";
    std::cout << "capacity: " << v.capacity() << "\n";
    std::cout << "v[4]: "     << v[4]         << "\n";

    // ── emplace_back ──────────────────────────────────────────────────
    folly::fbvector<std::string> sv;
    sv.reserve(4);
    sv.emplace_back("alpha");
    sv.emplace_back("beta");
    sv.emplace_back("gamma");

    for (const auto& s : sv) std::cout << s << " ";
    std::cout << "\n";

    // ── erase / insert (same as std::vector) ─────────────────────────
    v.erase(v.begin() + 2); // remove element at index 2
    v.insert(v.begin(), -1);

    std::cout << "after erase+insert: ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";

    // ── swap is O(1) ─────────────────────────────────────────────────
    folly::fbvector<int> a{1, 2, 3}, b{10, 20};
    a.swap(b);
    std::cout << "a after swap: ";
    for (int x : a) std::cout << x << " ";
    std::cout << "\n"; // 10 20
}
