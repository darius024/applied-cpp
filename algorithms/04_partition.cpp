#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Partition algorithms: reorder a range so elements satisfying a
// predicate come before those that do not.
// The returned iterator points to the first element of the "false" group.
//
// partition        — unstable: O(N), relative order may change.
// stable_partition — stable:   O(N log N), preserves relative order.
// partition_copy   — copies true/false elements to two separate outputs.
// is_partitioned   — O(N) check without modifying the range.
// ─────────────────────────────────────────────────────────────────────

void print(const std::vector<int>& v, const std::string& label = "")
{
    if (!label.empty()) std::cout << label << ": ";
    for (int x : v) std::cout << x << " ";
    std::cout << "\n";
}

int main()
{
    auto is_even = [](int x){ return x % 2 == 0; };

    // ── partition ─────────────────────────────────────────────────────
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8};
    auto bound = std::partition(v.begin(), v.end(), is_even);
    // evens first, odds second — relative order within each group may change
    print(v, "partitioned");
    std::cout << "first odd at index: " << std::distance(v.begin(), bound) << "\n";

    // ── stable_partition ──────────────────────────────────────────────
    // Relative order within each group is preserved.
    std::vector<int> v2 = {1, 2, 3, 4, 5, 6, 7, 8};
    std::stable_partition(v2.begin(), v2.end(), is_even);
    print(v2, "stable_partition"); // 2 4 6 8 | 1 3 5 7

    // ── partition_copy ────────────────────────────────────────────────
    // Copies into two separate output ranges instead of reordering in place.
    std::vector<int> v3 = {1, 2, 3, 4, 5, 6};
    std::vector<int> evens, odds;
    std::partition_copy(v3.begin(), v3.end(),
                        std::back_inserter(evens),
                        std::back_inserter(odds),
                        is_even);
    print(evens, "evens"); // 2 4 6
    print(odds,  "odds");  // 1 3 5

    // ── is_partitioned ────────────────────────────────────────────────
    std::cout << std::boolalpha;
    std::cout << "v2 is_partitioned(even): "
              << std::is_partitioned(v2.begin(), v2.end(), is_even) << "\n"; // true
    std::cout << "v3 is_partitioned(even): "
              << std::is_partitioned(v3.begin(), v3.end(), is_even) << "\n"; // false

    // ── Practical: separate passing/failing scores ─────────────────────
    struct Student { std::string name; int score; };
    std::vector<Student> students = {
        {"alice", 82}, {"bob", 45}, {"carol", 91}, {"dave", 58}, {"eve", 37}
    };
    auto pass_bound = std::stable_partition(
        students.begin(), students.end(), [](const Student& s){ return s.score >= 60; });

    std::cout << "passing: ";
    for (auto it = students.begin(); it != pass_bound; ++it)
        std::cout << it->name << "(" << it->score << ") ";
    std::cout << "\nfailing: ";
    for (auto it = pass_bound; it != students.end(); ++it)
        std::cout << it->name << "(" << it->score << ") ";
    std::cout << "\n";
}
