#include <iostream>
#include <type_traits>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Type traits — compile-time queries and transformations on types.
//
// All live in <type_traits>.
// Query traits inherit from std::true_type or std::false_type.
// Value shortcut:  std::is_same_v<T,U>    ≡  std::is_same<T,U>::value
// Type shortcut:   std::remove_ref_t<T>   ≡  std::remove_reference<T>::type
//
// Two categories:
//   Query:     is the type X? → bool
//   Transform: produce a related type → ::type
// ─────────────────────────────────────────────────────────────────────

// ── Standard query traits ─────────────────────────────────────────────

void show_queries()
{
    using T = const int&;

    std::cout << std::boolalpha;
    std::cout << "is_same<int,int>:      " << std::is_same_v<int, int>        << "\n";
    std::cout << "is_same<int,double>:   " << std::is_same_v<int, double>     << "\n";
    std::cout << "is_integral<int>:      " << std::is_integral_v<int>         << "\n";
    std::cout << "is_integral<double>:   " << std::is_integral_v<double>      << "\n";
    std::cout << "is_pointer<int*>:      " << std::is_pointer_v<int*>         << "\n";
    std::cout << "is_reference<T>:       " << std::is_reference_v<T>          << "\n";
    std::cout << "is_const<const int>:   " << std::is_const_v<const int>      << "\n";
    std::cout << "is_abstract<IShape>:   "; // shown below

    struct IShape { virtual void draw() = 0; virtual ~IShape()=default; };
    std::cout << std::is_abstract_v<IShape> << "\n";
}

// ── Standard transform traits ─────────────────────────────────────────

void show_transforms()
{
    // remove_reference: strips & and &&
    static_assert(std::is_same_v<std::remove_reference_t<int&>, int>);
    static_assert(std::is_same_v<std::remove_reference_t<int&&>, int>);

    // remove_cv: strips const and/or volatile
    static_assert(std::is_same_v<std::remove_cv_t<const volatile int>, int>);

    // decay: models pass-by-value: strips refs, cv, array→pointer, fn→ptr
    static_assert(std::is_same_v<std::decay_t<const int&>, int>);
    static_assert(std::is_same_v<std::decay_t<int[3]>,     int*>);

    // add_const / add_pointer
    static_assert(std::is_same_v<std::add_const_t<int>,   const int>);
    static_assert(std::is_same_v<std::add_pointer_t<int>,  int*>);

    std::cout << "transform static_asserts passed\n";
}

// ── Writing a custom type trait ───────────────────────────────────────
//
// Goal: is_iterable<T> — true if T has begin() and end().
// Uses void_t + expression SFINAE in the specialisation.

template<typename T, typename = void>
struct is_iterable : std::false_type {};

template<typename T>
struct is_iterable<T,
    std::void_t<
        decltype(std::begin(std::declval<T>())),
        decltype(std::end(std::declval<T>()))
    >> : std::true_type {};

template<typename T>
inline constexpr bool is_iterable_v = is_iterable<T>::value;

// ── Writing a custom transform trait ─────────────────────────────────
//
// make_const_ref<T>: strips existing ref/cv, then adds const&.
// Useful in generic wrappers that want to store by const reference.

template<typename T>
struct make_const_ref {
    using type = const std::remove_cvref_t<T>&;
};

template<typename T>
using make_const_ref_t = typename make_const_ref<T>::type;


int main()
{
    show_queries();
    std::cout << "\n";
    show_transforms();

    std::cout << "\n";
    std::cout << "is_iterable<vector<int>>: " << is_iterable_v<std::vector<int>> << "\n"; // 1
    std::cout << "is_iterable<string>:      " << is_iterable_v<std::string>      << "\n"; // 1
    std::cout << "is_iterable<int>:         " << is_iterable_v<int>              << "\n"; // 0

    static_assert(std::is_same_v<make_const_ref_t<int&&>, const int&>);
    static_assert(std::is_same_v<make_const_ref_t<const double>, const double&>);
    std::cout << "make_const_ref static_asserts passed\n";
}
