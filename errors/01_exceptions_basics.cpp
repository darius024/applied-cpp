#include <iostream>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Exception basics: throw, try/catch, catch hierarchy, rethrowing.
//
// When an exception is thrown the runtime unwinds the stack — every
// in-scope object's destructor runs — until a matching catch is found.
// If no handler exists, std::terminate is called.
// ─────────────────────────────────────────────────────────────────────

double divide(double a, double b)
{
    if (b == 0.0) throw std::invalid_argument("division by zero");
    return a / b;
}

// ── Catch hierarchy ───────────────────────────────────────────────────
// Handlers are checked TOP TO BOTTOM. Derived types must come first;
// a base-type handler shadows any derived-type handler below it.
void catch_order_demo(int choice)
{
    try {
        if (choice == 1) throw std::out_of_range("index 99");
        if (choice == 2) throw std::runtime_error("disk full");
        if (choice == 3) throw 42; // non-std type
    }
    catch (const std::out_of_range& e) {      // most-derived first
        std::cout << "out_of_range: " << e.what() << "\n";
    }
    catch (const std::runtime_error& e) {
        std::cout << "runtime_error: " << e.what() << "\n";
    }
    catch (const std::exception& e) {          // base catches the rest
        std::cout << "exception: " << e.what() << "\n";
    }
    catch (...) {                              // catch-all for non-std types
        std::cout << "unknown exception\n";
    }
}

// ── Rethrowing ────────────────────────────────────────────────────────
// throw;          rethrows the current exception — preserves the dynamic type.
// throw e;        throws a COPY of e — may slice a derived type to base.
void log_and_rethrow()
{
    try {
        throw std::runtime_error("something failed");
    }
    catch (const std::exception& e) {
        std::cout << "[log] caught: " << e.what() << "\n";
        throw; // rethrow — not throw e; — to preserve the type
    }
}

int main()
{
    // ── Basic throw/catch ─────────────────────────────────────────────
    try {
        std::cout << divide(10, 2) << "\n"; // 5
        std::cout << divide(10, 0) << "\n"; // throws
    }
    catch (const std::invalid_argument& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    // ── Catch order ───────────────────────────────────────────────────
    catch_order_demo(1);
    catch_order_demo(2);
    catch_order_demo(3);

    // ── Rethrow ───────────────────────────────────────────────────────
    try {
        log_and_rethrow();
    }
    catch (const std::runtime_error& e) {
        std::cout << "outer caught: " << e.what() << "\n";
    }
}
