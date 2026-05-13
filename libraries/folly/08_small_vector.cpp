#include <folly/small_vector.h>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 08_small_vector.cpp -o 08_small_vector -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::small_vector<T, N, PolicyFlags>: inline storage up to N,
// heap-allocated beyond N.
//
// When size ≤ N: the elements live directly inside the small_vector
// object — zero heap traffic, much better cache locality.
// When size > N: behaves exactly like std::vector (heap allocation).
//
// Common uses:
//   - Compiler IR: most basic blocks have ≤ 4 successors.
//   - Graph adjacency lists: most nodes have low degree.
//   - Event handler lists: usually 1-2 handlers per event.
//   - Any container where the "hot" case is small but the API must
//     handle arbitrary sizes.
//
// API is identical to std::vector.
// PolicyFlags (via folly::small_vector_policy) can restrict max size
// to save space (e.g. OneBitMutex, NoHeap for hard inline-only).
// ─────────────────────────────────────────────────────────────────────

// Show where elements live
template<typename SV>
void print_info(const char* label, const SV& v)
{
    std::cout << label
              << " size=" << v.size()
              << " isExtern=" << v.isExtern() // true = heap-allocated
              << " : ";
    for (const auto& x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── Inline storage (size ≤ N = 4) ────────────────────────────────
    folly::small_vector<int, 4> sv;
    sv.push_back(10);
    sv.push_back(20);
    sv.push_back(30);
    print_info("3 elements", sv); // isExtern=false

    // ── Spill to heap (size > 4) ──────────────────────────────────────
    sv.push_back(40);
    sv.push_back(50); // 5th element triggers heap allocation
    print_info("5 elements", sv); // isExtern=true

    // ── Back to inline after shrink ───────────────────────────────────
    // Note: shrinking does NOT move back to inline storage once spilled.
    sv.resize(3);
    print_info("shrunk to 3", sv); // still isExtern=true

    // ── String small_vector ───────────────────────────────────────────
    folly::small_vector<std::string, 2> words;
    words.emplace_back("alpha");
    words.emplace_back("beta");
    print_info("2 strings", words); // isExtern=false

    words.emplace_back("gamma");
    print_info("3 strings", words); // isExtern=true

    // ── NoHeap policy: compile-time guarantee — never heap-allocates ──
    // Throws std::length_error if you try to exceed N.
    folly::small_vector<int, 8, folly::small_vector_policy::NoHeap> fixed;
    for (int i = 0; i < 8; ++i) fixed.push_back(i);
    print_info("NoHeap (8)", fixed);
    try {
        fixed.push_back(99); // exceeds inline capacity
    } catch (const std::length_error& e) {
        std::cout << "NoHeap overflow: " << e.what() << "\n";
    }
}
