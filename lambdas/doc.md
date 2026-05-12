# Lambdas in C++

A lambda is an anonymous function object (a closure) defined inline.
The compiler generates a unique struct with `operator()` for each lambda.
No two lambdas have the same type, even if they look identical.

---

## Syntax

```
[ captures ] ( params ) specifiers -> return_type { body }
```

Only the capture list and body are mandatory; everything else is optional.

---

## Captures

| Syntax              | Meaning                                               |
|---------------------|-------------------------------------------------------|
| `[]`                | Capture nothing                                       |
| `[=]`               | Capture all locals by copy (at point of lambda definition) |
| `[&]`               | Capture all locals by reference                       |
| `[x]`               | Capture `x` by copy                                   |
| `[&x]`              | Capture `x` by reference                              |
| `[x = expr]`        | Init capture: compute `expr`, store as `x` (C++14)   |
| `[x = std::move(x)]`| Capture by move — transfers ownership into the closure|
| `[this]`            | Capture the current object pointer                    |
| `[*this]`           | Capture the current object by copy (C++17)            |

**`mutable`**: by default a copy-captured variable is `const` inside the lambda.
Add `mutable` to allow modification (the outer variable is still unchanged).

---

## Generic and templated lambdas

- **C++14 generic lambda**: `auto` parameter — the compiler generates `operator()` as a template.
- **C++20 templated lambda**: explicit `<typename T>` in the lambda, needed when you must name T.

---

## `std::function` vs function pointers vs template parameters

| Mechanism          | Type erased | Overhead  | Captures | Best when                          |
|--------------------|-------------|-----------|----------|------------------------------------|
| Template parameter | No          | Zero      | Yes      | Performance-critical, called once  |
| Function pointer   | Yes         | Minimal   | No       | C interop, no captures needed      |
| `std::function`    | Yes         | Heap alloc possible | Yes | Stored callbacks, runtime dispatch |

Rule: prefer a template parameter; use `std::function` only when you need to **store** the callable and the type cannot be a template parameter.

---

## IILE — Immediately Invoked Lambda Expression

```cpp
const int x = [&]{ /* complex init logic */ return value; }();
```

Lets you initialise a `const` variable with logic that requires multiple statements,
without needing a named helper function. Common for computing lookup tables, conditional
initialisations, and complex aggregations.

---

## Callbacks

A callback is a callable passed to a function to be invoked at a specific point.
- Pass by **template parameter** (fastest, inlined, can't be stored after function returns).
- Pass by **`std::function`** (type-erased, storable, small allocation overhead).
- The choice matters for hot paths; irrelevant for I/O-bound code.

---

## Higher-order functions

Functions that take or return functions.
`map`, `filter`, `reduce` (fold) are the canonical trio.
Combining them with lambdas replaces hand-written loops and is expressive,
but watch for unnecessary copies: pass ranges by reference where possible.

---

## Examples

| File                              | Covers                                                     |
|-----------------------------------|------------------------------------------------------------|
| `01_syntax_captures.cpp`          | All capture modes, mutable, init captures, dangling ref    |
| `02_generic_templated.cpp`        | `auto` params (C++14), `<typename T>` lambdas (C++20)      |
| `03_function_vs_fp_vs_template.cpp` | Cost comparison, when to use each                        |
| `04_iile.cpp`                     | Const init, lookup tables, complex branching               |
| `05_callbacks.cpp`                | Owning vs non-owning callbacks, event dispatcher           |
| `06_higher_order.cpp`             | map, filter, reduce, function composition                  |
