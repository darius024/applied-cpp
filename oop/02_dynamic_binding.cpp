#include <iostream>
#include <memory>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
// Dynamic binding: a virtual call through a base pointer/reference is
// resolved at RUNTIME using the vtable.
//
// Every polymorphic object carries a hidden vptr → vtable.
// The vtable is a per-class array of function pointers.
// A virtual call is: load vptr, index into vtable, indirect call.
// Cost: ~1 pointer indirection, prevents inlining.
// ─────────────────────────────────────────────────────────────────────

struct Animal {
    virtual void speak() const { std::cout << "Animal\n"; }
    virtual ~Animal() = default;
};

struct Dog : Animal {
    void speak() const override { std::cout << "Woof\n"; }
};

struct Cat : Animal {
    void speak() const override { std::cout << "Meow\n"; }
};

// speak() is resolved at runtime based on the actual object type.
void make_speak(const Animal& a) { a.speak(); }

// ─────────────────────────────────────────────────────────────────────
// Pitfall 1: object slicing.
// Passing a derived object BY VALUE to a base-typed parameter copies
// only the base part. The derived data and vtable are lost.
// Always use pointer or reference for polymorphic types.
// ─────────────────────────────────────────────────────────────────────

void sliced(Animal a)  { a.speak(); } // always prints "Animal" — derived part sliced off
void correct(const Animal& a) { a.speak(); } // correct: reference keeps vtable

// ─────────────────────────────────────────────────────────────────────
// Pitfall 2: non-virtual functions are NOT dispatched dynamically.
// If you call a non-virtual function through a base pointer, you always
// get the BASE version, regardless of the actual object type.
// ─────────────────────────────────────────────────────────────────────

struct Base {
    virtual void v()  const { std::cout << "Base::v  (virtual)\n"; }
            void nv() const { std::cout << "Base::nv (non-virtual)\n"; }
    virtual ~Base() = default;
};

struct Derived : Base {
    void v()  const override { std::cout << "Derived::v  (virtual)\n"; }
    void nv() const          { std::cout << "Derived::nv (non-virtual)\n"; }
};

int main()
{
    // ── Dynamic dispatch ────────────────────────────────────────────
    auto dog = std::make_unique<Dog>();
    auto cat = std::make_unique<Cat>();

    make_speak(*dog); // Woof
    make_speak(*cat); // Meow

    // ── Slicing ─────────────────────────────────────────────────────
    std::cout << "\n--- slicing ---\n";
    Dog d;
    sliced(d);  // Animal  ← sliced
    correct(d); // Woof    ← correct

    // ── Non-virtual dispatch ─────────────────────────────────────────
    std::cout << "\n--- non-virtual ---\n";
    Derived obj;
    const Base* p = &obj;

    p->v();   // Derived::v  — virtual: runtime dispatch
    p->nv();  // Base::nv    — non-virtual: compile-time, always Base
}
