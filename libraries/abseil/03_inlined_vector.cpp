#include <absl/container/inlined_vector.h>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 03_inlined_vector.cpp -o 03_inlined_vector \
//          -labsl_base -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// absl::InlinedVector<T, N>
//
// Stores up to N elements inline (inside the object itself, on the
// stack or in the containing struct). When size exceeds N, it spills
// to a heap allocation exactly like std::vector.
//
// API is identical to std::vector.
//
// Benefits of inline storage:
//   - Zero heap traffic for the common (small) case.
//   - Better cache locality: element data is adjacent to the object.
//   - No allocator overhead on construction / destruction.
//
// Choosing N:
//   Pick the value that covers ≥ 90% of real inputs. N=4 or N=8 are
//   common defaults. Larger N wastes stack space when the vector is
//   empty or small.
//
// Used extensively in TensorFlow (shape vectors, device lists),
// LLVM/Clang (SmallVector is the same idea), and gRPC.
// ─────────────────────────────────────────────────────────────────────

// Show whether storage is on heap or inline
template<typename IV>
void print_info(const char* label, const IV& v)
{
    std::cout << label
              << " size=" << v.size()
              << " capacity=" << v.capacity()
              << " : ";
    for (const auto& x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    // ── Inline storage (size ≤ N = 4) ────────────────────────────────
    absl::InlinedVector<int, 4> iv;
    iv.push_back(10);
    iv.push_back(20);
    iv.push_back(30);
    print_info("3 elements (inline)", iv);

    // ── Spill to heap (size > 4) ──────────────────────────────────────
    iv.push_back(40);
    iv.push_back(50); // 5th element triggers heap allocation
    print_info("5 elements (heap)", iv);

    // ── emplace_back ──────────────────────────────────────────────────
    absl::InlinedVector<std::string, 2> sv;
    sv.emplace_back("alpha");
    sv.emplace_back("beta");
    print_info("2 strings (inline)", sv);
    sv.emplace_back("gamma"); // spills
    print_info("3 strings (heap)", sv);

    // ── insert / erase ────────────────────────────────────────────────
    absl::InlinedVector<int, 8> v2{1, 2, 3, 4, 5};
    v2.insert(v2.begin() + 2, 99); // {1, 2, 99, 3, 4, 5}
    v2.erase(v2.begin());          // {2, 99, 3, 4, 5}
    print_info("after insert+erase", v2);

    // ── absl::Span interop ────────────────────────────────────────────
    // InlinedVector implicitly converts to absl::Span<T> and Span<const T>
    // via data()/size(), enabling zero-copy passing to span-accepting APIs.
    std::cout << "front: " << v2.front()
              << "  back: " << v2.back() << "\n";

    // ── swap ──────────────────────────────────────────────────────────
    absl::InlinedVector<int, 4> a{1, 2}, b{10, 20, 30};
    a.swap(b);
    print_info("a after swap", a); // 10 20 30
    print_info("b after swap", b); // 1 2
}
