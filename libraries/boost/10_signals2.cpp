#include <boost/signals2.hpp>
#include <iostream>
#include <string>

// compile: g++ -std=c++17 10_signals2.cpp -o 10_signals2

// ─────────────────────────────────────────────────────────────────────
// boost::signals2: thread-safe signal/slot (observer pattern). Header-only.
//
// signal<Signature> — callable that invokes all connected slots.
// connect(slot)     — returns a connection handle.
// scoped_connection — RAII disconnect: slot is removed when it goes
//                     out of scope (no manual disconnect needed).
// Groups            — integer group labels; lower group fires first.
// Combiners         — customise how return values from slots are merged.
// shared_connection_block — temporarily suppress a slot without disconnecting.
//
// HPC relevance: decouples event sources from handlers with zero
// dynamic allocation on the hot path after initial setup. Used in
// middleware, plugin systems, and reactive pipelines.
// ─────────────────────────────────────────────────────────────────────

namespace sig = boost::signals2;

// ── Custom combiner: sums all integer return values ───────────────────
struct Summer {
    using result_type = int;
    template<typename Iter>
    int operator()(Iter first, Iter last) const {
        int total = 0;
        while (first != last) total += *first++;
        return total;
    }
};

int main()
{
    // ── Basic connect / fire ──────────────────────────────────────────
    sig::signal<void(const std::string&)> on_event;

    auto c1 = on_event.connect([](const std::string& msg) {
        std::cout << "[listener1] " << msg << "\n";
    });
    auto c2 = on_event.connect([](const std::string& msg) {
        std::cout << "[listener2] " << msg << "\n";
    });

    on_event("startup");

    // ── Manual disconnect ─────────────────────────────────────────────
    c2.disconnect();
    on_event("only listener1 now");

    // ── scoped_connection: RAII disconnect ────────────────────────────
    {
        sig::scoped_connection sc = on_event.connect([](const std::string& msg) {
            std::cout << "[scoped] " << msg << "\n";
        });
        on_event("scoped slot is active");
    } // sc destroyed → slot disconnected automatically
    on_event("scoped slot is gone");

    // ── Group ordering (lower number fires first) ─────────────────────
    sig::signal<void()> ordered;
    ordered.connect(2, [] { std::cout << "group 2\n"; });
    ordered.connect(0, [] { std::cout << "group 0\n"; });
    ordered.connect(1, [] { std::cout << "group 1\n"; });
    std::cout << "ordered fire:\n";
    ordered(); // prints: group 0, group 1, group 2

    // ── Custom combiner: sum return values ────────────────────────────
    sig::signal<int(), Summer> on_score;
    on_score.connect([] { return 10; });
    on_score.connect([] { return 30; });
    on_score.connect([] { return 5;  });
    std::cout << "combined score: " << on_score() << "\n"; // 45

    // ── shared_connection_block: temporary suppression ────────────────
    sig::signal<void()> pulsed;
    auto conn = pulsed.connect([] { std::cout << "pulse!\n"; });

    pulsed(); // fires
    {
        sig::shared_connection_block block(conn); // suppress without disconnect
        pulsed();                                 // silent
    }
    pulsed(); // fires again
}
