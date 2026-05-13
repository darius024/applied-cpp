#include <boost/asio.hpp>
#include <iostream>
#include <chrono>

// compile: g++ -std=c++17 05_asio_timers.cpp -o 05_asio_timers -lboost_system -pthread

// ─────────────────────────────────────────────────────────────────────
// boost::asio: async I/O engine — the foundation of most C++ servers.
//
// io_context: the event loop. Handlers are queued here and run on
//   whatever thread(s) call io_context::run().
//
// steady_timer: schedule a callback after a duration or at a time point.
//   async_wait(handler) — registers the callback; returns immediately.
//   The handler fires when the timer expires or is cancelled.
//
// strand<>: serialise handlers from multiple threads so they never
//   run concurrently, without needing a mutex.
//
// Post / dispatch: inject arbitrary work into the event loop.
//   post(io, f)   — always queues f (runs later).
//   dispatch(io, f) — runs f inline if already on the io_context thread.
// ─────────────────────────────────────────────────────────────────────

namespace asio = boost::asio;
using namespace std::chrono_literals;

// Repeating timer: reschedules itself from inside its own handler.
// expires_after() is relative to NOW — use expires_at() for absolute.
void repeat(asio::steady_timer& t, int& count, int limit)
{
    t.expires_after(30ms);
    t.async_wait([&t, &count, limit](const boost::system::error_code& ec) {
        if (ec) return; // operation_aborted when timer is cancelled
        std::cout << "tick " << ++count << "\n";
        if (count < limit) repeat(t, count, limit);
    });
}

int main()
{
    asio::io_context io;

    // ── One-shot timer ────────────────────────────────────────────────
    asio::steady_timer t1(io, 100ms);
    t1.async_wait([](const boost::system::error_code& ec) {
        if (!ec) std::cout << "one-shot fired\n";
    });

    // ── Cancellable timer ─────────────────────────────────────────────
    // cancel() causes the handler to fire immediately with error code
    // boost::asio::error::operation_aborted.
    asio::steady_timer t2(io, 10s);
    t2.async_wait([](const boost::system::error_code& ec) {
        if (ec == asio::error::operation_aborted)
            std::cout << "timer cancelled as expected\n";
    });
    t2.cancel();

    // ── Repeating timer ───────────────────────────────────────────────
    asio::steady_timer t3(io);
    int count = 0;
    repeat(t3, count, 3); // fires 3 times, ~30ms apart

    // ── Strand: serialise posted tasks across any number of threads ───
    // Without a strand, two threads calling io.run() could execute
    // handlers concurrently. A strand guarantees serialisation.
    auto strand = asio::make_strand(io);
    asio::post(strand, [] { std::cout << "strand task A\n"; });
    asio::post(strand, [] { std::cout << "strand task B\n"; }); // never overlaps A
    asio::post(strand, [] { std::cout << "strand task C\n"; });

    io.run(); // run until all handlers complete
}
