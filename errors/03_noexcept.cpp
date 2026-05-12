#include <iostream>
#include <vector>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// noexcept: promise that a function will never throw.
// If it does throw, std::terminate is called — no stack unwinding.
//
// Why it matters:
//   std::vector and other containers use the MOVE constructor during
//   reallocation (e.g. push_back). They will ONLY use the move constructor
//   if it is noexcept; otherwise they fall back to copy, because a throwing
//   move would leave the original in a partially-moved-from state with no
//   way to recover.
//
// Rule: always mark move constructors and move assignment noexcept.
// ─────────────────────────────────────────────────────────────────────

// ── noexcept on free functions ─────────────────────────────────────────
int safe_add(int a, int b) noexcept { return a + b; }

// ── noexcept(expr): conditional noexcept ─────────────────────────────
// Useful in generic code: the wrapper is noexcept iff the operation it
// calls is noexcept. noexcept(expr) evaluates at compile time to bool.
template<typename T>
void swap_safe(T& a, T& b) noexcept(noexcept(std::swap(a, b)))
{
    std::swap(a, b);
}

// ── Effect on std::vector: copy vs move ─────────────────────────────

struct WithMove
{
    int val;
    explicit WithMove(int v) : val(v) {}

    WithMove(const WithMove&)            = default;
    WithMove(WithMove&&) noexcept        = default; // marked noexcept → vector uses move
    WithMove& operator=(WithMove&&) noexcept = default;
};

struct WithoutNoexcept
{
    int val;
    explicit WithoutNoexcept(int v) : val(v) {}

    WithoutNoexcept(const WithoutNoexcept&) = default;
    WithoutNoexcept(WithoutNoexcept&&)      = default; // NOT noexcept → vector uses copy
    WithoutNoexcept& operator=(WithoutNoexcept&&) = default;
};

int main()
{
    // ── noexcept() operator: compile-time query ────────────────────────
    std::cout << std::boolalpha;
    std::cout << "safe_add noexcept: "
              << noexcept(safe_add(1, 2)) << "\n"; // true

    int a = 1, b = 2;
    std::cout << "swap_safe<int> noexcept: "
              << noexcept(swap_safe(a, b)) << "\n"; // true

    // ── vector reallocation: move iff noexcept ────────────────────────
    // Use std::move_if_noexcept to see the compiler's decision:
    //   WithMove&& → moves (noexcept)
    //   const WithoutNoexcept& → copies (no noexcept guarantee)
    WithMove       wm{1};
    WithoutNoexcept wn{2};
    std::cout << "WithMove move_if_noexcept is rvalue: "
              << std::is_rvalue_reference_v<decltype(std::move_if_noexcept(wm))> << "\n"; // true
    std::cout << "WithoutNoexcept move_if_noexcept is rvalue: "
              << std::is_rvalue_reference_v<decltype(std::move_if_noexcept(wn))> << "\n"; // false

    // ── Violating noexcept calls std::terminate (commented — would crash) ──
    // auto bad = []() noexcept { throw std::runtime_error("oops"); };
    // bad(); // std::terminate — no unwinding
}
