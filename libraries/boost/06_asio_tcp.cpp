#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <thread>

// compile: g++ -std=c++17 06_asio_tcp.cpp -o 06_asio_tcp -lboost_system -pthread

// ─────────────────────────────────────────────────────────────────────
// Async TCP echo server + synchronous client demo.
//
// Server pattern — three-layer async chain:
//   Server::accept() → Session::read() → Session::write() → read() → …
//
// shared_ptr + enable_shared_from_this: the Session keeps itself alive
// for as long as any async operation references it. When the connection
// closes and no more handlers are pending, the ref count drops to zero
// and the Session is destroyed automatically — no manual lifetime management.
//
// All handlers run on the single thread that calls io.run() so there is
// no shared-state race inside a Session. For a multi-thread server:
// call io.run() from N threads and protect per-session state with a
// strand (see 05_asio_timers.cpp).
// ─────────────────────────────────────────────────────────────────────

namespace asio = boost::asio;
using     tcp  = asio::ip::tcp;

// ── Session: one per accepted connection ──────────────────────────────
class Session : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(tcp::socket sock) : socket_(std::move(sock)) {}

    void start() { read(); }

private:
    void read()
    {
        auto self = shared_from_this(); // extend lifetime across async gap
        socket_.async_read_some(asio::buffer(buf_),
            [this, self](boost::system::error_code ec, std::size_t n) {
                if (!ec) write(n);
                // on error (EOF, reset): self destructs when lambda goes out of scope
            });
    }

    void write(std::size_t n)
    {
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(buf_, n),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) read(); // echo done — wait for next message
            });
    }

    tcp::socket socket_;
    char        buf_[1024];
};

// ── Server: accepts connections and spawns Sessions ───────────────────
class Server
{
public:
    Server(asio::io_context& io, unsigned short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port))
    {
        accept();
    }

private:
    void accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket sock) {
                if (!ec)
                    std::make_shared<Session>(std::move(sock))->start();
                accept(); // immediately queue the next accept
            });
    }

    tcp::acceptor acceptor_;
};

int main()
{
    asio::io_context io;
    Server server(io, 9000);
    std::cout << "echo server on :9000\n";

    // Run server on a background thread
    std::thread server_thread([&io] { io.run(); });

    // ── Synchronous client ────────────────────────────────────────────
    asio::io_context client_io;
    tcp::socket sock(client_io);
    tcp::resolver resolver(client_io);
    asio::connect(sock, resolver.resolve("127.0.0.1", "9000"));

    std::string msg = "hello from client\n";
    asio::write(sock, asio::buffer(msg));

    char reply[256]{};
    std::size_t n = sock.read_some(asio::buffer(reply));
    std::cout << "echoed: " << std::string(reply, n);

    sock.close();
    io.stop();
    server_thread.join();
}
