#include <iostream>
#include <string>
#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────────────
// Lambda syntax and every capture mode.
//
// The compiler generates a unique struct with operator() for each lambda.
// The capture list specifies which locals from the enclosing scope the
// closure "closes over" and how (by copy or by reference).
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── No capture ───────────────────────────────────────────────────
    auto greet = []{ std::cout << "hello\n"; };
    greet();

    // ── Capture by copy [x] ──────────────────────────────────────────
    // The value is copied at the point the lambda is DEFINED, not called.
    int counter = 0;
    auto snap = [counter]{ std::cout << "snap=" << counter << "\n"; };
    counter = 99;
    snap(); // prints 0, not 99 — captured the old value

    // ── Capture by reference [&x] ────────────────────────────────────
    // The lambda holds a reference to the original variable.
    auto inc = [&counter]{ ++counter; };
    inc(); inc();
    std::cout << "counter=" << counter << "\n"; // 101

    // ── Capture all by copy [=] ───────────────────────────────────────
    std::string prefix = "item";
    int         id     = 7;
    auto label = [=]{ return prefix + "-" + std::to_string(id); };
    prefix = "CHANGED"; id = 999;
    std::cout << label() << "\n"; // item-7 — copied at definition

    // ── Capture all by reference [&] ─────────────────────────────────
    // Convenient but dangerous: the lambda must not outlive the locals.
    std::vector<int> v = {1, 2, 3};
    auto sum_ref = [&]{ int s=0; for (int x:v) s+=x; return s; };
    v.push_back(4);
    std::cout << "sum=" << sum_ref() << "\n"; // 10 — sees the update

    // ── mutable ───────────────────────────────────────────────────────
    // By default, copy-captured variables are const inside the lambda.
    // mutable removes that const, allowing modification of the copy.
    // The ORIGINAL variable is unchanged.
    int x = 10;
    auto mutating = [x]() mutable {
        x += 5;                        // modifies the copy, not the original
        std::cout << "inner x=" << x << "\n";
    };
    mutating(); // inner x=15
    mutating(); // inner x=20 — the copy persists between calls
    std::cout << "outer x=" << x << "\n"; // 10 — untouched

    // ── Init capture [var = expr] (C++14) ─────────────────────────────
    // Compute an arbitrary expression and store it under a new name.
    // Essential for capturing move-only types (unique_ptr, future, etc.).
    auto ptr = std::make_unique<int>(42);
    auto owns = [p = std::move(ptr)]{ // ptr is now null; p owns the int
        std::cout << "owned value=" << *p << "\n";
    };
    owns();
    // ptr is null here — ownership was moved into the closure

    // ── Dangling reference — the classic bug ─────────────────────────
    // Storing a [&] lambda that outlives its captured locals is UB.
    // Shown as a comment — never do this:
    //
    //   std::function<void()> bad;
    //   {
    //       int local = 5;
    //       bad = [&local]{ std::cout << local; }; // local dies here
    //   }
    //   bad(); // UB: dangling reference
    std::cout << "(dangling ref example intentionally skipped)\n";

    // ── [this] and [*this] ────────────────────────────────────────────
    struct Widget {
        int value = 10;

        // [this] captures the pointer — lambda shares the object's lifetime.
        auto make_getter()  { return [this]{ return value; }; }

        // [*this] copies the object into the closure (C++17).
        // Safe to use after the Widget is destroyed.
        auto make_snapshot(){ return [*this]{ return value; }; }
    };

    Widget w;
    auto getter   = w.make_getter();
    auto snapshot = w.make_snapshot();
    w.value = 99;
    std::cout << "getter="   << getter()   << "\n"; // 99 — sees change via this
    std::cout << "snapshot=" << snapshot() << "\n"; // 10 — owns its own copy
}
