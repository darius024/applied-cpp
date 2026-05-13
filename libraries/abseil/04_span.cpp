#include <absl/types/span.h>
#include <iostream>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>

// compile: g++ -std=c++17 04_span.cpp -o 04_span -labsl_base

// ─────────────────────────────────────────────────────────────────────
// absl::Span<T>
//
// A non-owning, contiguous view: just a pointer + size.
// Span<const T> is implicitly constructible from:
//   - std::vector<T>
//   - std::array<T, N>
//   - T[]  (with known size)
//   - absl::InlinedVector<T, N>
//
// Span<T> (mutable) requires an explicit construction from a pointer
// or from a non-const container.
//
// Why use Span instead of a vector reference:
//   - A function taking Span<const int> accepts any contiguous storage
//     without overloading or template parameters.
//   - No copy. No allocation. Passed by value (two words: ptr + size).
//   - subspan(offset, count) slices without copying.
//
// std::span (C++20) is the standardised equivalent; use absl::Span
// for C++17 codebases or when you want consistent tooling with the
// rest of Abseil.
// ─────────────────────────────────────────────────────────────────────

// One function accepts any contiguous container — no templates needed.
int sum(absl::Span<const int> data)
{
    return std::accumulate(data.begin(), data.end(), 0);
}

void scale(absl::Span<double> data, double factor)
{
    for (double& x : data) x *= factor;
}

int main()
{
    // ── Implicit construction from vector ─────────────────────────────
    std::vector<int> v{1, 2, 3, 4, 5};
    std::cout << "sum from vector: " << sum(v) << "\n"; // 15

    // ── Implicit construction from array ──────────────────────────────
    std::array<int, 4> arr{10, 20, 30, 40};
    std::cout << "sum from array:  " << sum(arr) << "\n"; // 100

    // ── Implicit construction from raw array ──────────────────────────
    int raw[] = {7, 8, 9};
    std::cout << "sum from raw[]:  " << sum(raw) << "\n"; // 24

    // ── Mutable span ──────────────────────────────────────────────────
    std::vector<double> vals{1.0, 2.0, 3.0, 4.0};
    scale(absl::MakeSpan(vals), 2.5);
    std::cout << "scaled: ";
    for (double x : vals) std::cout << x << " "; // 2.5 5.0 7.5 10.0
    std::cout << "\n";

    // ── subspan: zero-copy slice ──────────────────────────────────────
    absl::Span<const int> sp(v);
    auto mid = sp.subspan(1, 3); // elements at index 1, 2, 3
    std::cout << "subspan: ";
    for (int x : mid) std::cout << x << " "; // 2 3 4
    std::cout << "\n";

    // ── first / last ──────────────────────────────────────────────────
    std::cout << "first(2): ";
    for (int x : sp.first(2)) std::cout << x << " "; // 1 2
    std::cout << "\n";

    std::cout << "last(2): ";
    for (int x : sp.last(2)) std::cout << x << " "; // 4 5
    std::cout << "\n";

    // ── Bounds info ───────────────────────────────────────────────────
    std::cout << "size: "  << sp.size()  << "\n"; // 5
    std::cout << "empty: " << std::boolalpha << sp.empty() << "\n";
    std::cout << "front: " << sp.front() << "  back: " << sp.back() << "\n";
}
