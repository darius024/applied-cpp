#include <iostream>
#include <type_traits>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// SFINAE — Substitution Failure Is Not An Error.
//
// When the compiler substitutes a type into a template and the
// substitution produces an ill-formed expression, that overload/
// specialisation is silently dropped from the candidate set — no error.
// The next viable overload is tried instead.
//
// std::enable_if<Condition, T>::type exploits this: if Condition is
// false, the nested ::type doesn't exist, substitution fails, and the
// overload is removed from consideration.
//
// Note: C++20 concepts replace most SFINAE patterns.
// Understand SFINAE to read existing production code; write new code
// with concepts where C++20 is available.
// ─────────────────────────────────────────────────────────────────────


// ── Pattern 1: enable_if in the return type ───────────────────────────
//
// Two overloads of describe(): one for integral types, one for floating point.
// Without SFINAE, both would match every arithmetic type and cause ambiguity.

template<typename T>
std::enable_if_t<std::is_integral_v<T>, std::string>
describe(T) { return "integral"; }

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, std::string>
describe(T) { return "floating point"; }


// ── Pattern 2: enable_if as a defaulted template parameter ───────────
//
// Cleaner for constructors (which have no return type) and for cases
// where you don't want to change the visible return type.

template<typename T,
         typename = std::enable_if_t<std::is_arithmetic_v<T>>>
void print_number(T v)
{
    std::cout << "number: " << v << "\n";
}

// This overload is only viable for non-arithmetic types (strings, etc.).
template<typename T,
         typename = std::enable_if_t<!std::is_arithmetic_v<T>>,
         typename = void>   // extra param disambiguates from the overload above
void print_number(const T& v)
{
    std::cout << "non-number: " << v << "\n";
}


// ── Pattern 3: detection idiom with void_t ────────────────────────────
//
// Detect whether a type has a member function .size() at compile time.
// Primary: assume it doesn't.
template<typename T, typename = void>
struct has_size : std::false_type {};

// Specialisation: if T{}.size() is a valid expression, this wins.
// std::void_t maps any well-formed expression to void, enabling the match.
template<typename T>
struct has_size<T, std::void_t<decltype(std::declval<T>().size())>>
    : std::true_type {};

template<typename T>
inline constexpr bool has_size_v = has_size<T>::value;

// Use the trait to select behaviour.
template<typename T>
void print_info(const T& v)
{
    if constexpr (has_size_v<T>)
        std::cout << "has size: " << v.size() << "\n";
    else
        std::cout << "no size() member\n";
}


int main()
{
    // ── enable_if in return type ─────────────────────────────────────
    std::cout << describe(42)    << "\n"; // integral
    std::cout << describe(3.14)  << "\n"; // floating point
    // describe("hi") — would be a compile error: no matching overload

    // ── enable_if as template param ──────────────────────────────────
    print_number(10);
    print_number(2.5f);
    print_number(std::string("hello"));

    // ── detection idiom ──────────────────────────────────────────────
    std::cout << "\n";
    std::cout << "vector has size: " << has_size_v<std::vector<int>> << "\n"; // 1
    std::cout << "int has size:    " << has_size_v<int>              << "\n"; // 0

    std::vector<int> v = {1, 2, 3};
    print_info(v);   // has size: 3
    print_info(42);  // no size() member
}
