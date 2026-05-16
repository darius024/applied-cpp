// compile: g++ -std=c++20 -O2 -o 01_generator 01_generator.cpp
// requires: GCC 11+ or Clang 12+

#include <coroutine>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Generator<T> — pull-based lazy sequence via co_yield.
//
// The caller "pulls" values one at a time; no value is computed until
// requested. Memory usage is O(1) regardless of sequence length.
//
// Lifecycle:
//   1. Caller calls the coroutine function → coroutine suspends at
//      initial_suspend (before any body runs).
//   2. Iterator::operator++ resumes the coroutine.
//   3. Body executes until the next co_yield v.
//   4. yield_value(v) stores v in the promise and suspends again.
//   5. Caller reads the value via promise().current_value.
//   6. Repeat until the body returns → final_suspend → done() == true.
// ─────────────────────────────────────────────────────────────────────

template<typename T>
class Generator
{
public:
    struct promise_type
    {
        T current_value;

        Generator get_return_object() noexcept
        {
            return Generator{handle_type::from_promise(*this)};
        }

        // Lazy: don't run the body until the first pull
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Keep the frame alive at the end so the iterator can observe done()
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield v → store v, suspend
        std::suspend_always yield_value(T v) noexcept
        {
            current_value = std::move(v);
            return {};
        }

        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    // ── Iterator ─────────────────────────────────────────────────────
    struct iterator
    {
        handle_type h;

        // Advance: resume until next yield or completion
        iterator& operator++()
        {
            h.resume();
            return *this;
        }

        T const& operator*() const { return h.promise().current_value; }
        T&       operator*()       { return h.promise().current_value; }

        // Sentinel comparison: done when coroutine body returned
        bool operator==(std::default_sentinel_t) const { return h.done(); }
    };

    iterator begin()
    {
        handle_.resume(); // run to first yield
        return iterator{handle_};
    }

    std::default_sentinel_t end() { return {}; }

    // ── Manual pull API ───────────────────────────────────────────────
    bool next()
    {
        handle_.resume();
        return !handle_.done();
    }
    T const& value() const { return handle_.promise().current_value; }

    // ── Lifecycle ─────────────────────────────────────────────────────
    explicit Generator(handle_type h) noexcept : handle_(h) {}
    Generator(Generator&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ~Generator() { if (handle_) handle_.destroy(); }

    Generator(const Generator&)            = delete;
    Generator& operator=(const Generator&) = delete;

private:
    handle_type handle_;
};

// ─────────────────────────────────────────────────────────────────────
// Examples
// ─────────────────────────────────────────────────────────────────────

// Infinite Fibonacci sequence
Generator<long long> fibonacci()
{
    long long a = 0, b = 1;
    while (true) {
        co_yield a;
        a = std::exchange(b, a + b);
    }
}

// Integer range [start, end) with step
Generator<int> range(int start, int stop, int step = 1)
{
    for (int i = start; i < stop; i += step)
        co_yield i;
}

// Yield items from any container
template<typename Container>
Generator<typename Container::value_type> from(const Container& c)
{
    for (const auto& v : c)
        co_yield v;
}

// Coroutines compose: a generator can co_yield values from another
Generator<int> squared_range(int n)
{
    auto r = range(1, n + 1);
    for (int v : r)
        co_yield v * v;
}

int main()
{
    // ── Range-for ────────────────────────────────────────────────────
    std::cout << "range(0, 10, 2): ";
    for (int v : range(0, 10, 2))
        std::cout << v << " ";
    std::cout << "\n";

    // ── First 10 Fibonacci numbers ───────────────────────────────────
    std::cout << "fibonacci: ";
    auto fib = fibonacci();
    for (int i = 0; i < 10; ++i) {
        fib.next();
        std::cout << fib.value() << " ";
    }
    std::cout << "\n";

    // ── From container ───────────────────────────────────────────────
    std::vector<std::string> names = {"Alice", "Bob", "Carol"};
    std::cout << "names: ";
    for (const auto& n : from(names))
        std::cout << n << " ";
    std::cout << "\n";

    // ── Composed generator ───────────────────────────────────────────
    std::cout << "squares 1-5: ";
    for (int v : squared_range(5))
        std::cout << v << " ";
    std::cout << "\n";

    // ── Manual pull: read until we find a Fibonacci > 100 ────────────
    auto f2 = fibonacci();
    while (f2.next() && f2.value() <= 100)
        ;
    std::cout << "first Fibonacci > 100: " << f2.value() << "\n";
}
