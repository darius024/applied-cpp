# Algorithms in C++

The `<algorithm>` and `<numeric>` headers provide over 100 generic algorithms
that operate on iterator ranges `[first, last)`. C++20 adds `<ranges>` with
constrained, composable pipeline versions of most of them.

---

## Iterator ranges

Every classic algorithm takes a half-open range `[first, last)` where `last`
is one past the end. Algorithms never own the container — they only read or
write through iterators.

**Complexity** is expressed in terms of the number of elements `N = last - first`.

---

## Sorting & ordering

| Algorithm       | Complexity       | Notes                                        |
|-----------------|------------------|----------------------------------------------|
| `sort`          | O(N log N) avg   | Not stable. Uses introsort internally.        |
| `stable_sort`   | O(N log² N)      | Preserves order of equal elements.            |
| `partial_sort`  | O(N log M)       | Only sorts the first M elements.              |
| `nth_element`   | O(N) avg         | Element at position n is correct; rest unordered. |

A **comparator** is any callable `(a, b) → bool` satisfying strict weak ordering:
irreflexive, asymmetric, transitive. The default is `operator<` (ascending).

---

## Searching

- `find` / `find_if` — linear scan; returns iterator to first match or `end`.
- `find_first_of` — first element in `[first1,last1)` that matches any element in `[first2,last2)`.
- `search` — finds a sub-sequence inside a range.
- `adjacent_find` — first pair of consecutive equal (or predicate-equal) elements.

All return `end()` on failure — check before dereferencing.

---

## Binary search (sorted ranges only)

Requires the range to be sorted by the same comparator used in the call.

| Algorithm      | Returns                                              |
|----------------|------------------------------------------------------|
| `binary_search`| `bool` — is the value present?                       |
| `lower_bound`  | Iterator to first element **not less than** value    |
| `upper_bound`  | Iterator to first element **greater than** value     |
| `equal_range`  | Pair of `{lower_bound, upper_bound}` — the sub-range of equal elements |

---

## Filtering & partitioning

**Partition**: reorder elements so all satisfying the predicate come before
all that don't. The boundary is returned as an iterator.

- `partition` — unstable (relative order may change).
- `stable_partition` — preserves relative order; O(N log N).
- `partition_copy` — copies true/false elements into two separate output ranges.
- `is_partitioned` — checks without reordering.

---

## Remove / erase

`remove` and `remove_if` do **not** shrink the container. They move elements
to-be-kept to the front and return an iterator to the new logical end.
You must call `erase` to actually shrink:

```cpp
v.erase(std::remove_if(v.begin(), v.end(), pred), v.end()); // erase-remove idiom
```

C++20: `std::erase_if(v, pred)` does both in one call.

`unique` removes *consecutive* duplicates. Sort first if you want all duplicates removed.

---

## Transforming

- `transform(in, in_end, out, f)` — apply unary `f` to each element, write to `out`.
- `transform(in1, in1_end, in2, out, f)` — binary version: combine two ranges.
- `for_each` — like transform but for side effects; return value of `f` is discarded.
- `replace_if(first, last, pred, new_val)` — overwrite matching elements in place.
- `fill` / `generate` — write a fixed value / call a generator for each element.

---

## Numeric algorithms `<numeric>`

| Algorithm        | What it computes                                        |
|------------------|---------------------------------------------------------|
| `accumulate`     | Left fold: `init op e1 op e2 …` (sequential)           |
| `reduce`         | Same but may reorder operands — parallelisable (C++17)  |
| `partial_sum`    | Running total: `a[i] = a[0]+…+a[i]`                    |
| `inclusive_scan` | Like partial_sum but with custom op and parallel hint   |
| `inner_product`  | Dot product: `Σ a[i] * b[i]`                           |
| `iota`           | Fill with incrementing values starting from `init`      |

`accumulate` is strictly left-to-right; `reduce` requires the operation to
be associative and commutative (or results are unspecified).

---

## Predicates & counting

- `any_of` / `all_of` / `none_of` — short-circuit O(N) scan.
- `count` / `count_if` — number of matching elements.
- `equal` — pairwise equality of two ranges.
- `mismatch` — first position where two ranges differ; returns pair of iterators.

---

## Min / max & heap

- `min_element` / `max_element` — O(N) scan.
- `minmax_element` — both in one pass.
- `clamp(v, lo, hi)` — returns `lo` if `v < lo`, `hi` if `v > hi`, else `v`.

A **heap** is a range where `front()` is always the maximum. Heap operations:
`make_heap`, `push_heap` (after `push_back`), `pop_heap` (before `pop_back`),
`sort_heap` (heap → sorted ascending, destroys heap property).

---

## Set operations (sorted ranges)

All require sorted input. Produce sorted output.

`set_union`, `set_intersection`, `set_difference`, `set_symmetric_difference`.

Output range must be pre-allocated or use `std::back_inserter`.

---

## Permutations & reordering

- `next_permutation` / `prev_permutation` — advance to next/prev lexicographic order; returns `false` when wrapped.
- `reverse` — in-place reversal.
- `rotate(first, n_first, last)` — `n_first` becomes the new beginning.
- `shuffle` — random reorder using a `UniformRandomBitGenerator`.
- `copy` / `copy_if` / `move` — transfer elements to output range.

---

## Ranges (C++20) `<ranges>`

`std::ranges::` versions of all classic algorithms:
- Accept containers directly (no `.begin()` / `.end()`).
- Use **projections**: `ranges::sort(v, {}, &Person::age)` sorts by field without a lambda.
- Are **constrained** — compile errors are clearer.

**Views** are lazy, composable range adaptors:

```cpp
auto result = v | views::filter(pred) | views::transform(f) | views::take(5);
```

No allocation; elements are computed on demand when you iterate.

| View             | Effect                                              |
|------------------|-----------------------------------------------------|
| `views::filter`  | Skip elements not satisfying predicate              |
| `views::transform`| Map a function over each element                  |
| `views::take(n)` | First N elements                                    |
| `views::drop(n)` | Skip first N elements                               |
| `views::iota(a,b)`| Generates integers [a, b)                         |
| `views::reverse` | Iterate in reverse                                  |
| `views::zip`     | Pair elements from two ranges (C++23)               |
| `views::enumerate`| (index, value) pairs (C++23)                      |
