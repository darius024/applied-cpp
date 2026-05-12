#include <iostream>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// CRTP — Curiously Recurring Template Pattern.
//
// Idiom: a base class is parameterised by the derived class itself.
//
//   template<typename Derived>
//   struct Base { ... static_cast<Derived*>(this)->method() ... };
//
//   struct MyClass : Base<MyClass> { ... };
//
// The base can call derived methods via static_cast — resolved at COMPILE
// time. No vtable, no vptr, no indirect call overhead. Fully inlinable.
//
// Primary use: MIXINS — inject reusable behaviour into a class by
// inheriting from a templated base. Each instantiation is its own type,
// so there is no shared base pointer (unlike virtual polymorphism).
// ─────────────────────────────────────────────────────────────────────


// ── Mixin 1: Comparable ──────────────────────────────────────────────
//
// The derived class provides only operator== and operator<.
// Comparable<T> generates the other four for free.
// The standard library equivalent is std::rel_ops (deprecated) or
// C++20 operator<=> with std::totally_ordered.

template<typename Derived>
struct Comparable {
    // All comparisons derived from == and < — no virtual calls.
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
    friend bool operator> (const Derived& a, const Derived& b) { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};

struct Temperature : Comparable<Temperature> {
    double celsius;
    explicit Temperature(double c) : celsius(c) {}

    bool operator==(const Temperature& o) const { return celsius == o.celsius; }
    bool operator< (const Temperature& o) const { return celsius <  o.celsius; }
};


// ── Mixin 2: Printable ───────────────────────────────────────────────
//
// The derived class provides to_string().
// Printable<T> injects print() and operator<< without virtual dispatch.

template<typename Derived>
struct Printable {
    void print() const {
        std::cout << static_cast<const Derived*>(this)->to_string() << "\n";
    }

    friend std::ostream& operator<<(std::ostream& os, const Derived& d) {
        return os << d.to_string();
    }
};

struct Point : Printable<Point> {
    int x, y;
    Point(int x, int y) : x(x), y(y) {}

    std::string to_string() const {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};


// ── Mixin 3: InstanceCounter ─────────────────────────────────────────
//
// Each concrete type T gets its own independent static counter.
// Because Base<Dog> and Base<Cat> are different template instantiations,
// their static members are separate — Dog and Cat are counted independently.

template<typename Derived>
struct InstanceCounter {
    InstanceCounter()  { ++count_; }
    ~InstanceCounter() { --count_; }

    static int count() { return count_; }

private:
    inline static int count_ = 0; // C++17 inline static
};

struct Dog : InstanceCounter<Dog> { std::string name; Dog(std::string n) : name(n) {} };
struct Cat : InstanceCounter<Cat> { std::string name; Cat(std::string n) : name(n) {} };


int main()
{
    // ── Comparable ───────────────────────────────────────────────────
    Temperature t1(20.0), t2(37.0);
    std::cout << std::boolalpha;
    std::cout << "t1 < t2:  " << (t1 < t2)  << "\n"; // true  — provided by us
    std::cout << "t1 > t2:  " << (t1 > t2)  << "\n"; // false — injected by CRTP
    std::cout << "t1 <= t2: " << (t1 <= t2) << "\n"; // true  — injected by CRTP
    std::cout << "t1 != t2: " << (t1 != t2) << "\n"; // true  — injected by CRTP
    std::cout << "\n";

    // ── Printable ────────────────────────────────────────────────────
    Point p(3, 4);
    p.print();
    std::cout << "point: " << p << "\n\n";

    // ── InstanceCounter ──────────────────────────────────────────────
    {
        Dog d1("Rex"), d2("Buddy");
        Cat c1("Whiskers");
        std::cout << "dogs: " << Dog::count() << "\n"; // 2
        std::cout << "cats: " << Cat::count() << "\n"; // 1
    }
    std::cout << "dogs after scope: " << Dog::count() << "\n"; // 0
}
