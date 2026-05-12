#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Transforming algorithms.
//
// transform(in, in_end, out, f)          — unary: apply f to each element.
// transform(in1, in1_end, in2, out, f)   — binary: combine two ranges with f.
// for_each(first, last, f)               — like transform but for side effects;
//                                          return value of f is discarded.
// replace_if(first, last, pred, new_val) — overwrite matching elements in place.
// fill(first, last, val)                 — set every element to val.
// fill_n(first, n, val)                  — fill n elements from first.
// generate(first, last, gen)             — call gen() for each element.
// generate_n(first, n, gen)              — call gen() n times.
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    std::vector<int> v = {1, 2, 3, 4, 5};

    // ── transform: unary ─────────────────────────────────────────────
    std::vector<int> doubled(v.size());
    std::transform(v.begin(), v.end(), doubled.begin(),
                   [](int x){ return x * 2; });
    print(doubled, "doubled"); // 2 4 6 8 10

    // Writing into the same range (in-place transform):
    std::transform(v.begin(), v.end(), v.begin(),
                   [](int x){ return x * x; });
    print(v, "squared in-place"); // 1 4 9 16 25

    // ── transform: binary ─────────────────────────────────────────────
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {10, 20, 30};
    std::vector<int> sum(3);
    std::transform(a.begin(), a.end(), b.begin(), sum.begin(),
                   [](int x, int y){ return x + y; });
    print(sum, "a+b"); // 11 22 33

    // ── for_each ─────────────────────────────────────────────────────
    // Use when the purpose is a side effect (printing, accumulating into
    // an external variable), not producing a new range.
    int total = 0;
    std::for_each(a.begin(), a.end(), [&total](int x){ total += x; });
    std::cout << "total=" << total << "\n"; // 6

    // ── replace_if ────────────────────────────────────────────────────
    std::vector<int> v2 = {1, 2, 3, 4, 5, 6};
    std::replace_if(v2.begin(), v2.end(),
                    [](int x){ return x % 2 == 0; }, 0);
    print(v2, "replace evens with 0"); // 1 0 3 0 5 0

    // ── fill ──────────────────────────────────────────────────────────
    std::vector<int> v3(5);
    std::fill(v3.begin(), v3.end(), 7);
    print(v3, "fill 7"); // 7 7 7 7 7

    // ── generate ──────────────────────────────────────────────────────
    // Call a stateful generator for each element.
    std::vector<int> v4(6);
    int n = 0;
    std::generate(v4.begin(), v4.end(), [&n]{ return n += 10; });
    print(v4, "generate +10"); // 10 20 30 40 50 60
}
