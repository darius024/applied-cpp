#include <iostream>
#include <numeric>
#include <vector>
#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────
// Numeric algorithms from <numeric>.
//
// accumulate(first, last, init)        — left fold: init op e0 op e1 …
//                                        Sequential, always left-to-right.
// reduce(first, last, init)            — same but MAY reorder (C++17).
//                                        Requires associative+commutative op.
// partial_sum(first, last, out)        — running total: out[i] = sum(in[0..i]).
// inclusive_scan(first, last, out, op) — like partial_sum with custom op (C++17).
// exclusive_scan(first, last, out, init, op) — out[i] = op of elements BEFORE i.
// inner_product(f1,l1, f2, init)       — dot product: init + Σ a[i]*b[i].
// iota(first, last, val)               — fill with val, val+1, val+2, …
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

    // ── accumulate ────────────────────────────────────────────────────
    int sum = std::accumulate(v.begin(), v.end(), 0);
    std::cout << "sum=" << sum << "\n"; // 15

    int product = std::accumulate(v.begin(), v.end(), 1, std::multiplies<int>{});
    std::cout << "product=" << product << "\n"; // 120

    // Concatenate strings via accumulate (custom op):
    std::vector<std::string> words = {"hello", " ", "world"};
    std::string sentence = std::accumulate(words.begin(), words.end(), std::string{});
    std::cout << sentence << "\n"; // hello world

    // ── reduce (C++17) ────────────────────────────────────────────────
    // May process elements in any order — result same only if op is
    // associative and commutative (e.g. addition, multiplication).
    int r = std::reduce(v.begin(), v.end(), 0);
    std::cout << "reduce sum=" << r << "\n"; // 15

    // ── partial_sum ───────────────────────────────────────────────────
    std::vector<int> running(v.size());
    std::partial_sum(v.begin(), v.end(), running.begin());
    print(running, "partial_sum"); // 1 3 6 10 15

    // ── inclusive_scan / exclusive_scan (C++17) ───────────────────────
    std::vector<int> inc(v.size()), exc(v.size());
    std::inclusive_scan(v.begin(), v.end(), inc.begin()); // out[i] includes v[i]
    std::exclusive_scan(v.begin(), v.end(), exc.begin(), 0); // out[i] excludes v[i]
    print(inc, "inclusive_scan"); // 1 3 6 10 15
    print(exc, "exclusive_scan"); // 0 1 3 6 10

    // ── inner_product ─────────────────────────────────────────────────
    // Default: init + Σ a[i]*b[i]  (dot product)
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {4, 5, 6};
    int dot = std::inner_product(a.begin(), a.end(), b.begin(), 0);
    std::cout << "dot product=" << dot << "\n"; // 1*4+2*5+3*6 = 32

    // Custom ops: sum of element-wise max (add + max instead of + and *)
    int sum_max = std::inner_product(a.begin(), a.end(), b.begin(), 0,
                      std::plus<int>{},
                      [](int x, int y){ return std::max(x, y); });
    std::cout << "sum of max pairs=" << sum_max << "\n"; // 4+5+6 = 15

    // ── iota ──────────────────────────────────────────────────────────
    std::vector<int> idx(6);
    std::iota(idx.begin(), idx.end(), 1); // fill with 1,2,3,4,5,6
    print(idx, "iota(1)"); // 1 2 3 4 5 6
}
