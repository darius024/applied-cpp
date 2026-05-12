#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// `override` and `final` are contextual keywords (not reserved words —
// you can still have variables named `override`).
//
// override: tells the compiler "this function must override a virtual
//           in a base class". If the signatures don't match exactly,
//           it's a compile error instead of a silent new virtual.
//
// final:    on a virtual method — no further overrides allowed.
//           on a class — no further inheritance allowed. Also a hint
//           to the compiler that it can devirtualise calls.
// ─────────────────────────────────────────────────────────────────────


// ── Pitfall: accidental override failure ────────────────────────────
//
// A tiny signature mismatch silently declares a NEW virtual instead of
// overriding. The base version is called when you expected the derived one.

struct Logger {
    virtual void log(const char* msg) { std::cout << "Logger: " << msg << "\n"; }
    virtual ~Logger() = default;
};

struct BadDerived : Logger {
    // Typo: `const char*` became `const char* const` — different signature.
    // No compile error. log() in Logger is NEVER called via BadDerived.
    // This is a silent bug that `override` would have caught.
    virtual void log(const char* const msg) { std::cout << "BadDerived: " << msg << "\n"; }
};

struct GoodDerived : Logger {
    // `override` forces the compiler to verify the signature matches.
    // Change the signature → compile error. No silent new virtual.
    void log(const char* msg) override { std::cout << "GoodDerived: " << msg << "\n"; }
};


// ── `final` on a method ──────────────────────────────────────────────
//
// Prevents further specialisation of a specific virtual in subclasses.
// Useful when a mid-level class provides a concrete implementation that
// must not be changed further down the hierarchy.

struct Transport {
    virtual void connect() = 0;
    virtual ~Transport() = default;
};

struct TcpTransport : Transport {
    void connect() final override      // no subclass can override connect() again
    {
        std::cout << "TCP connect\n";
    }
};

// struct BrokenTcp : TcpTransport {
//     void connect() override {}      // compile error: connect() is final
// };


// ── `final` on a class ───────────────────────────────────────────────
//
// Seals the class entirely. No subclasses allowed.
// The compiler can devirtualise ALL virtual calls on objects of this type
// (since there are no derived classes, the vtable is unnecessary).

struct SslTransport final : Transport {
    void connect() override { std::cout << "SSL connect\n"; }
};

// struct BadSsl : SslTransport {}; // compile error: SslTransport is final


int main()
{
    Logger* b = new BadDerived();
    Logger* g = new GoodDerived();

    b->log("test"); // calls Logger::log — BadDerived's version is NOT called
    g->log("test"); // calls GoodDerived::log — correctly overridden

    delete b;
    delete g;

    TcpTransport tcp;
    SslTransport ssl;
    tcp.connect();
    ssl.connect();
}
