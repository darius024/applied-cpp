#include <iostream>
#include <type_traits>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Metaprogramming building blocks.
//
// These are the primitives you compose to build type-level logic.
// Understanding them lets you read any production template library.
//
//   std::conditional_t   — compile-time ternary (pick one of two types)
//   std::void_t          — maps any valid expression list to void
//   std::type_identity   — delays type deduction
//   std::integral_constant — the base of all type traits
// ─────────────────────────────────────────────────────────────────────


// ── std::conditional_t — compile-time ternary ─────────────────────────
//
// conditional_t<B, T, F> is T when B==true, F when B==false.

template<typename T>
using StorageType = std::conditional_t<
    sizeof(T) <= sizeof(void*),
    T,              // small type: store by value
    const T&        // large type: store by const ref
>;

void show_conditional()
{
    static_assert(std::is_same_v<StorageType<int>,         int>);
    static_assert(std::is_same_v<StorageType<std::string>, const std::string&>);

    // Nested: pick the widest integer type that fits.
    using Int16or32 = std::conditional_t<sizeof(int) >= 4, int, long>;
    std::cout << "Int16or32 size: " << sizeof(Int16or32) << "\n";
    std::cout << "conditional static_asserts passed\n";
}


// ── std::void_t — detection idiom ────────────────────────────────────
//
// void_t<Exprs...> is void if all Exprs are well-formed, else substitution fails.
// This lets you detect whether a type supports an operation.

// Does T have a reserve() method (like vector, string)?
template<typename T, typename = void>
struct has_reserve : std::false_type {};

template<typename T>
struct has_reserve<T, std::void_t<decltype(std::declval<T>().reserve(0))>>
    : std::true_type {};

template<typename T>
inline constexpr bool has_reserve_v = has_reserve<T>::value;

// Use the trait to call reserve() only when available.
template<typename Container>
void try_reserve(Container& c, std::size_t n)
{
    if constexpr (has_reserve_v<Container>) {
        c.reserve(n);
        std::cout << "reserved " << n << " slots\n";
    } else {
        std::cout << "no reserve() available\n";
    }
}


// ── std::type_identity — delay deduction ──────────────────────────────
//
// type_identity<T> wraps T in a non-deducible context.
// Useful to force a caller to be explicit about one argument while
// letting the other be deduced.

// Without type_identity: scale(1, 2.0) fails — T can't be both int and double.
// With type_identity: T is deduced from `value`, `factor` must convert to T.
template<typename T>
T scale(T value, std::type_identity_t<T> factor)
{
    return value * factor;
}


// ── std::integral_constant — the root of type traits ─────────────────
//
// Every query trait is (ultimately) an integral_constant<bool, V>.
// You can build your own numeric compile-time constants the same way.

template<int N>
using Int = std::integral_constant<int, N>;

template<int A, int B>
using Add = Int<A + B>;

void show_integral_constant()
{
    constexpr int result = Add<3, 4>::value;
    std::cout << "3 + 4 at compile time: " << result << "\n";

    // Type-level boolean
    using IsEven = std::integral_constant<bool, (result % 2 == 0)>;
    std::cout << "result is even: " << IsEven::value << "\n";
}


int main()
{
    show_conditional();
    std::cout << "\n";

    std::cout << "vector has reserve: " << has_reserve_v<std::vector<int>> << "\n";
    std::cout << "list   has reserve: " << has_reserve_v<std::list<int>>   << "\n";

    std::vector<int> v;
    std::list<int>   l;
    try_reserve(v, 100);
    try_reserve(l, 100);
    std::cout << "\n";

    std::cout << "scale(5, 2.0) = " << scale(5, 2) << "\n";
    // scale(5, 2.0) would be a compile error without type_identity:
    // T deduced as int from first arg, so second must also be int.
    std::cout << "\n";

    show_integral_constant();
}
