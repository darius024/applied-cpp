#include <iostream>
#include <type_traits>
#include <vector>
#include <string>
#include <utility>

// ─────────────────────────────────────────────────────────────────────
// decltype, auto return types, and std::declval.
//
// decltype(expr)  — the type of expr, without evaluating it.
//                   Two modes:
//                     decltype(variable)   → the declared type (no ref added)
//                     decltype((variable)) → lvalue reference to the type
//
// auto return     — deduce return type from the return statement (like a variable).
//                   Use trailing `->` when the return type depends on template params.
//
// std::declval<T>()
//                 — produces a value of type T in an unevaluated context.
//                   Lets you inspect member types/return types without constructing T.
// ─────────────────────────────────────────────────────────────────────


// ── decltype basics ───────────────────────────────────────────────────

void show_decltype()
{
    int x = 5;
    decltype(x)    a = 10;   // int     (declared type of x)
    decltype((x))  b = x;    // int&    (lvalue ref — double parens)
    decltype(x+1)  c = 0;    // int     (type of the expression x+1)

    static_assert(std::is_same_v<decltype(a), int>);
    static_assert(std::is_same_v<decltype(b), int&>);
    std::cout << "a=" << a << " b=" << b << " c=" << c << "\n";
}


// ── Trailing return types ─────────────────────────────────────────────
//
// Before C++14 auto deduction, trailing `->` was the only way to express
// a return type that depends on template arguments.
// Still required when the expression is in scope only after the parameters.

template<typename T, typename U>
auto add(T t, U u) -> decltype(t + u)   // return type is whatever t+u gives
{
    return t + u;
}

// C++14: auto alone works when the body has a single unambiguous return.
template<typename Container>
auto front(Container& c) { return c.front(); }  // deduced from c.front()

// decltype(auto): preserves refs/cv from the return expression.
// auto alone would strip them (return by value).
template<typename Container>
decltype(auto) front_ref(Container& c) { return c.front(); } // returns ref if front() returns ref


// ── std::declval ──────────────────────────────────────────────────────
//
// Lets you "use" a type in a decltype context without constructing it.
// Essential for types with no default constructor, or for inspecting
// member function return types at compile time.

struct NoCtor {
    NoCtor() = delete;
    int compute() const { return 42; }
};

// We can query the return type of NoCtor::compute() without constructing NoCtor.
using ComputeResult = decltype(std::declval<NoCtor>().compute()); // int

// Building a trait: what does T::transform() return?
template<typename T>
using TransformResult = decltype(std::declval<T>().transform());


// ── Perfect forwarding with decltype(auto) ────────────────────────────
//
// A wrapper that forwards the return value category (lvalue/rvalue) exactly.
// Without decltype(auto), returning by auto always copies.

template<typename F, typename... Args>
decltype(auto) call(F&& f, Args&&... args)
{
    return std::forward<F>(f)(std::forward<Args>(args)...);
}


int main()
{
    // ── decltype ─────────────────────────────────────────────────────
    show_decltype();

    // ── trailing return / auto deduction ─────────────────────────────
    std::cout << add(1, 2)        << "\n";  // int
    std::cout << add(1, 2.5)      << "\n";  // double
    std::cout << add(std::string("hello "), std::string("world")) << "\n";

    std::vector<int> v = {10, 20, 30};
    front(v) = 99;                          // copy — modifying copy, not original
    front_ref(v) = 99;                      // ref  — modifies v[0]
    std::cout << "v[0] after front_ref = " << v[0] << "\n"; // 99

    // ── declval ───────────────────────────────────────────────────────
    static_assert(std::is_same_v<ComputeResult, int>);
    std::cout << "ComputeResult is int: " << std::is_same_v<ComputeResult, int> << "\n";

    // ── call wrapper ──────────────────────────────────────────────────
    auto result = call([](int a, int b){ return a * b; }, 6, 7);
    std::cout << "call result: " << result << "\n"; // 42
}
