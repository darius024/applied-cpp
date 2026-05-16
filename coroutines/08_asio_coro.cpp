// compile: g++ -std=c++20 -O2 -pthread -lboost_system -o 08_asio_coro 08_asio_coro.cpp
// requires: GCC 11+ or Clang 12+, Boost 1.75+  (brew install boost)

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────
// Asio coroutines: asio::awaitable<T> + co_spawn.
//
// asio::awaitable<T>
//   The coroutine return type for Asio-managed coroutines.
//   Internally: same promise_type + coroutine_handle pattern,
//   integrated with Asio's executor model.
//
// co_spawn(executor, coroutine, completion_token)
//   Submits a coroutine to an executor (io_context, thread_pool, etc.).
//   Completion tokens: asio::detached (fire-and-forget),
//                      asio::use_future (get a std::future<T>),
//                      another coroutine handle.
//
// Asio async operations accept use_awaitable as a completion token,
// which transforms them into awaitables:
//   co_await socket.async_read_some(buffer, use_awaitable);
//   co_await async_write(socket, buffer, use_awaitable);
//   co_await timer.async_wait(use_awaitable);
//
// The io_context drives everything: its run() loop processes both I/O
// events and coroutine resumes on the same thread — no locking needed.
// ─────────────────────────────────────────────────────────────────────

namespace asio = boost::asio;
using     tcp  = asio::ip::tcp;
using     awaitable_void = asio::awaitable<void>;

// ── Echo session: one coroutine per client connection ─────────────────
awaitable_void echo_session(tcp::socket socket)
{
    std::array<char, 1024> buf;
    try {
        for (;;) {
            // Suspend until data arrives; no thread blocked
            std::size_t n = co_await socket.async_read_some(
                asio::buffer(buf), asio::use_awaitable);

            // Echo back
            co_await asio::async_write(
                socket,
                asio::buffer(buf.data(), n),
                asio::use_awaitable);
        }
    } catch (const std::exception& e) {
        // EOF or error: session ends cleanly
        std::cout << "[server] session closed: " << e.what() << "\n";
    }
}

// ── Acceptor loop: listens and spawns a session per connection ────────
awaitable_void acceptor_loop(tcp::acceptor& acc)
{
    for (;;) {
        tcp::socket socket = co_await acc.async_accept(asio::use_awaitable);
        std::cout << "[server] accepted connection\n";

        // Spawn each session as an independent coroutine on the same executor
        asio::co_spawn(
            acc.get_executor(),
            echo_session(std::move(socket)),
            asio::detached);
    }
}

// ── Timer demo: await a timer without blocking a thread ──────────────
awaitable_void timer_demo(asio::io_context& ctx)
{
    asio::steady_timer timer(ctx);
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);
    std::cout << "[timer] fired after 50ms\n";
}

// ── Client: connect, send, receive ────────────────────────────────────
awaitable_void echo_client(asio::io_context& ctx,
                            const std::string& host,
                            unsigned short port)
{
    // Small delay to let server start
    asio::steady_timer delay(ctx);
    delay.expires_after(std::chrono::milliseconds(20));
    co_await delay.async_wait(asio::use_awaitable);

    tcp::socket socket(ctx);
    tcp::resolver resolver(ctx);

    auto endpoints = co_await resolver.async_resolve(
        host, std::to_string(port), asio::use_awaitable);

    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
    std::cout << "[client] connected\n";

    // Send message
    std::string msg = "hello coroutine world";
    co_await asio::async_write(socket, asio::buffer(msg), asio::use_awaitable);

    // Receive echo
    std::array<char, 256> buf{};
    std::size_t n = co_await socket.async_read_some(
        asio::buffer(buf), asio::use_awaitable);

    std::cout << "[client] echo: " << std::string(buf.data(), n) << "\n";

    socket.close();
}

int main()
{
    asio::io_context ctx;

    // ── Timer demo (no network) ────────────────────────────────────────
    asio::co_spawn(ctx, timer_demo(ctx), asio::detached);
    ctx.run();
    ctx.restart();

    // ── Echo server + client ───────────────────────────────────────────
    const unsigned short PORT = 12345;

    tcp::acceptor acceptor(ctx, {tcp::v4(), PORT});
    std::cout << "[server] listening on port " << PORT << "\n";

    // Launch acceptor loop
    asio::co_spawn(ctx, acceptor_loop(acceptor), asio::detached);

    // Launch client (will close acceptor after one exchange)
    asio::co_spawn(ctx,
        [&]() -> awaitable_void {
            co_await echo_client(ctx, "127.0.0.1", PORT);
            // Stop the io_context to end the demo
            acceptor.close();
            ctx.stop();
        }(),
        asio::detached);

    ctx.run();
    std::cout << "[main] done\n";
}
