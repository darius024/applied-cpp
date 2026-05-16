// compile: g++ -std=c++20 -O2 -o 07_pipeline 07_pipeline.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <iostream>
#include <optional>
#include <functional>
#include <string>
#include <vector>
#include <utility>

// ─────────────────────────────────────────────────────────────────────
// Generator pipelines: lazy data transformation without intermediate
// containers.
//
// Each adaptor is itself a generator that pulls from an upstream
// generator. Values flow on demand — nothing is computed until the
// consumer asks for the next element.
//
//   source | map(f) | filter(p) | take(n)
//
// This is the coroutine analog of C++20 ranges::views, but implemented
// from scratch to show the mechanics.
//
// Key property: O(1) memory regardless of pipeline length or source
// size. Ideal for streaming processing in HPC pipelines.
// ─────────────────────────────────────────────────────────────────────

// Generator<T> from 01_generator.cpp (repeated here for self-containment)
template<typename T>
class Generator
{
public:
    struct promise_type
    {
        std::optional<T> current;

        Generator get_return_object() noexcept
        {
            return Generator{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }

        std::suspend_always yield_value(T v) noexcept
        {
            current = std::move(v);
            return {};
        }

        void return_void()          noexcept {}
        void unhandled_exception()  noexcept { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    struct iterator
    {
        handle_type h;
        iterator& operator++() { h.resume(); return *this; }
        T const& operator*() const { return *h.promise().current; }
        bool operator==(std::default_sentinel_t) const { return h.done(); }
    };

    iterator begin() { handle_.resume(); return {handle_}; }
    std::default_sentinel_t end() { return {}; }

    bool next() { handle_.resume(); return !handle_.done(); }
    T const& value() const { return *handle_.promise().current; }

    explicit Generator(handle_type h) noexcept : handle_(h) {}
    Generator(Generator&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Generator() { if (handle_) handle_.destroy(); }
    Generator(const Generator&) = delete;

private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// Adaptors
// ─────────────────────────────────────────────────────────────────────

// map: transform each element with a function
template<typename T, typename F>
Generator<std::invoke_result_t<F, T>> map(Generator<T> src, F fn)
{
    for (auto& v : src)
        co_yield fn(v);
}

// filter: pass elements satisfying a predicate
template<typename T, typename P>
Generator<T> filter(Generator<T> src, P pred)
{
    for (auto& v : src)
        if (pred(v))
            co_yield v;
}

// take: yield at most n elements
template<typename T>
Generator<T> take(Generator<T> src, std::size_t n)
{
    std::size_t count = 0;
    for (auto& v : src) {
        if (count++ >= n) break;
        co_yield v;
    }
}

// drop: skip the first n elements
template<typename T>
Generator<T> drop(Generator<T> src, std::size_t n)
{
    std::size_t skipped = 0;
    for (auto& v : src) {
        if (skipped++ < n) continue;
        co_yield v;
    }
}

// zip: pair elements from two generators (stops at the shorter one)
template<typename A, typename B>
Generator<std::pair<A, B>> zip(Generator<A> a, Generator<B> b)
{
    auto ia = a.begin(), ib = b.begin();
    while (ia != std::default_sentinel && ib != std::default_sentinel) {
        co_yield {*ia, *ib};
        ++ia; ++ib;
    }
}

// scan: running accumulator (like std::inclusive_scan, but lazy)
template<typename T, typename U, typename F>
Generator<U> scan(Generator<T> src, U init, F combine)
{
    U acc = std::move(init);
    for (auto& v : src) {
        acc = combine(acc, v);
        co_yield acc;
    }
}

// ─────────────────────────────────────────────────────────────────────
// Source generators
// ─────────────────────────────────────────────────────────────────────

Generator<int> iota(int start = 0)
{
    for (int i = start; ; ++i)
        co_yield i;
}

Generator<int> of(std::vector<int> v)
{
    for (int x : v)
        co_yield x;
}

// ─────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────

template<typename T>
void print_all(Generator<T> gen, const std::string& label)
{
    std::cout << label << ": ";
    for (const auto& v : gen)
        std::cout << v << " ";
    std::cout << "\n";
}

template<typename T>
std::vector<T> collect(Generator<T> gen)
{
    std::vector<T> out;
    for (auto& v : gen)
        out.push_back(v);
    return out;
}

int main()
{
    // map: square first 5 naturals
    print_all(
        map(take(iota(1), 5), [](int x){ return x * x; }),
        "squares 1-5"
    );

    // filter: even numbers in [0,20)
    print_all(
        filter(take(iota(0), 20), [](int x){ return x % 2 == 0; }),
        "evens 0-18"
    );

    // drop + take: elements 5..9 of iota
    print_all(
        take(drop(iota(0), 5), 5),
        "iota[5..9]"
    );

    // Composed pipeline: naturals → square → keep if > 10 → first 5
    print_all(
        take(
            filter(
                map(iota(1), [](int x){ return x * x; }),
                [](int x){ return x > 10; }
            ),
            5
        ),
        "squares > 10"
    );

    // zip two generators
    {
        auto letters = of({'a', 'b', 'c', 'd', 'e'});
        // zip with iota (indices)
        std::cout << "zip(letters, 0..): ";
        for (auto& [c, i] : zip(of({'a','b','c','d','e'}), take(iota(0), 5)))
            std::cout << c << i << " ";
        std::cout << "\n";
    }

    // Running sum
    print_all(
        scan(take(iota(1), 6), 0, std::plus<int>{}),
        "running sum 1-6"
    );
}
