# OOP Patterns in C++

---

## Core concepts

### Abstract interfaces
A class with only pure virtual functions (`= 0`) and a virtual destructor.
It defines a contract — it holds no data and has no behaviour of its own.
The virtual destructor is mandatory: deleting a derived object through a base pointer
without it is undefined behaviour.

### Dynamic binding (virtual dispatch)
When a virtual function is called through a pointer or reference to base, the call is
resolved at runtime via the **vtable** — a per-class table of function pointers.
Each object carries a hidden `vptr` pointing to its class's vtable.
Cost: one pointer indirection per virtual call; prevents inlining.
Use virtual only when you genuinely need runtime polymorphism.

### Inheritance vs composition
- **Inheritance (is-a)**: `Circle` is a `Shape`. The derived class IS a specialisation of the base.
- **Composition (has-a)**: `Car` has an `Engine`. Prefer this by default — it is less coupled, easier to test, and avoids the fragile base class problem.
- Inheriting from concrete classes (especially STL containers) is almost always wrong.

### Operator overloading
Rules of thumb:
- Operators that modify `*this` (`+=`, `-=`, `++`) → **member functions**
- Symmetric binary operators (`+`, `==`, `<`) → **free functions** (both operands are equal citizens)
- `<<`, `>>` → must be **free functions** (left operand is `ostream`, not your type)
- Implement `+` in terms of `+=`; implement `!=`, `>`, `<=`, `>=` in terms of `==` and `<`

### Copy-and-swap idiom
The canonical way to write a safe copy-assignment operator for a resource-owning class:
1. Copy-construct a temporary from the argument (reuses the copy constructor).
2. Swap `*this` with the temporary (noexcept, cheap).
3. Let the temporary's destructor release the old resource.

Gives strong exception safety and handles self-assignment for free.

### Non-Virtual Interface (NVI)
Make the **public API non-virtual**; call private virtual hooks internally.
Benefits: the base class can enforce pre/post-conditions, logging, or locking
without derived classes being able to bypass them.
The public interface is stable; only the extension point is virtual.

### `override` and `final`
- `override`: compiler error if the function does not actually override a virtual — catches typos in signatures.
- `final` on a method: no further overrides allowed.
- `final` on a class: no further inheritance allowed; enables devirtualisation optimisations.

### Factory method
Return `unique_ptr<Base>` from a factory function.
Callers depend only on the abstract interface; concrete types are never exposed.
A map-based registry lets new types be added without touching the factory itself.

### CRTP — Curiously Recurring Template Pattern
Static polymorphism: `Base<Derived>` is parameterised by the derived class.
The base can call `static_cast<Derived*>(this)->method()` — resolved at compile time,
zero vtable cost, inlinable. Used to write mixins that inject reusable behaviour
(e.g. deriving `!=`, `>`, `<=`, `>=` from just `==` and `<`).

---

## Examples

| File                              | Covers                                          |
|-----------------------------------|-------------------------------------------------|
| `01_interfaces.cpp`               | Pure virtual, virtual destructor, interface contract |
| `02_dynamic_binding.cpp`          | Vtable dispatch, slicing, non-virtual pitfall   |
| `03_inheritance_vs_composition.cpp` | Is-a vs has-a, fragile base class problem     |
| `04_operator_overloading.cpp`     | Vec2: `+`, `==`, `<`, `<<`, member vs free      |
| `05_copy_and_swap.cpp`            | Safe copy assignment, strong exception guarantee |
| `06_nvi.cpp`                      | Non-Virtual Interface, enforced pre-conditions  |
| `07_override_final.cpp`           | Silent override bugs, `override`, `final`       |
| `08_factory.cpp`                  | Factory returning `unique_ptr<Base>`, registry  |
| `09_crtp.cpp`                     | Comparable mixin, per-type instance counter     |
