# Templates, Concepts & Type Utilities in C++

---

## Core templates

### Function & class templates
A template is a blueprint the compiler stamps out for each type it's used with.
Each stamped-out version is a distinct type or function — there is no shared code at runtime.
`typename` and `class` are interchangeable in template parameter lists; `typename` is preferred.
Template argument deduction: the compiler infers `T` from the call arguments — you rarely need to write it explicitly.

### Template specialisation
**Full specialisation**: a completely different implementation for one specific type.
**Partial specialisation**: a different implementation for a family of types (e.g. all pointer types).
Only class templates (not function templates) support partial specialisation — use overloading for functions.
Type traits are built from specialisation: `is_pointer<T>` is false by default; the `T*` specialisation sets it true.

### Variadic templates
A template that accepts any number of type parameters: `template<typename... Ts>`.
`Ts...` is a **parameter pack**; it must be expanded with `...`.
**Fold expressions** (C++17) collapse a pack over a binary operator without recursion:
`(args + ...)` expands to `arg0 + arg1 + arg2 + ...`.
Before fold expressions, recursion with a base-case specialisation was the only option.

---

## Advanced templates

### SFINAE — Substitution Failure Is Not An Error
When the compiler substitutes a type into a template and the substitution produces an invalid expression,
it silently discards that overload instead of emitting an error.
`std::enable_if<condition, T>::type` exploits this: if `condition` is false the type doesn't exist,
SFINAE discards the overload, and a different one is selected.
Verbose and error-prone — C++20 concepts replace most SFINAE.

### `if constexpr`
Compile-time branch inside a template body.
The discarded branch is not instantiated — it can contain code that would not compile for other types.
Replaces the most common single-function SFINAE patterns cleanly (C++17).

### Template template parameters & policy classes
A template parameter can itself be a template: `template<typename T, template<typename> class Container>`.
Lets callers swap entire data structures or algorithms (policies) at compile time with zero overhead.
Classic example: `Stack<int, std::vector>` vs `Stack<int, std::deque>`.

---

## Concepts (C++20)

### Concepts basics
A concept is a named boolean predicate on types, evaluated at compile time.
`requires` introduces an ad-hoc constraint; `concept` names a reusable one.
Constraint violations produce readable errors at the call site instead of deep template instantiation noise.

### Standard concepts
`<concepts>`: `std::integral`, `std::floating_point`, `std::same_as`, `std::derived_from`, `std::convertible_to`, `std::invocable`.
`<ranges>`: `std::ranges::range`, `std::ranges::sized_range`, `std::ranges::input_range`.
Write your own by combining requires-expressions and existing concepts with `&&` and `||`.

---

## Type utilities

### Type traits (`<type_traits>`)
Compile-time queries and transformations on types.
Query: `std::is_same_v<T,U>`, `std::is_pointer_v<T>`, `std::is_base_of_v<B,D>`.
Transform: `std::remove_reference_t<T>`, `std::decay_t<T>`, `std::add_const_t<T>`.
`std::decay_t` models what happens to a type when passed by value: strips refs, cv-qualifiers, array-to-pointer decay.

### Metaprogramming building blocks
`std::conditional_t<B, T, F>` — picks `T` if `B`, else `F`. The compile-time ternary.
`std::void_t<...>` — maps any well-formed type list to `void`; used to detect whether an expression is valid.
`std::type_identity<T>` — identity wrapper; delays deduction in function templates.

### `decltype`, `auto`, `std::declval`
`decltype(expr)` — the type of an expression, without evaluating it.
`auto` return type deduction — let the compiler deduce; use `->` trailing return for dependent types.
`std::declval<T>()` — produces a value of type T in an unevaluated context (inside `decltype`) without requiring a constructor.

---

## Examples

| File                            | Covers                                                        |
|---------------------------------|---------------------------------------------------------------|
| `01_function_class_templates.cpp` | Syntax, deduction, explicit instantiation, non-type params  |
| `02_specialisation.cpp`         | Full & partial specialisation, hand-rolled type trait         |
| `03_variadic.cpp`               | Parameter packs, fold expressions, recursive unpacking        |
| `04_sfinae.cpp`                 | `enable_if`, detection idiom, constraining overloads          |
| `05_if_constexpr.cpp`           | Compile-time branching, replacing SFINAE                      |
| `06_policy_classes.cpp`         | Template template params, policy-based Stack                  |
| `07_concepts_basics.cpp`        | `concept`, `requires`, constraining templates                 |
| `08_standard_concepts.cpp`      | stdlib concepts, writing custom compound concepts             |
| `09_type_traits.cpp`            | Query & transform traits, writing `is_iterable<T>`            |
| `10_metaprogramming.cpp`        | `conditional`, `void_t`, `type_identity`, detection idiom     |
| `11_decltype_auto.cpp`          | `decltype`, trailing returns, `declval`, perfect forwarding   |
