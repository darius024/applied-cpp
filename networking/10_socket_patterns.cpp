#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Socket patterns: RAII fd wrapper, length-framed protocol, reconnect
// with exponential backoff, a simple connection pool, and a non-blocking
// connect with timeout.
//
// These patterns appear in production order-entry and market data clients:
//   - RAII socket: never leak fds, even on exception paths
//   - Length-framing: turn a byte-stream into messages (TCP has no
//     message boundaries — you must add your own)
//   - Exponential backoff: avoid thundering herd on server restart
//   - Connection pool: reuse TCP connections for repeated RPC calls
//   - Timeout connect: detect unreachable hosts without blocking forever
//
// Compile: g++ -std=c++20 10_socket_patterns.cpp -o sock_patterns && ./sock_patterns
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using MS    = std::chrono::milliseconds;

// ── PART 1: RAII socket wrapper ───────────────────────────────────────
// Owns a file descriptor; closes it in the destructor.
// Moveable but not copyable (like unique_ptr).
class Socket {
public:
    static constexpr int INVALID = -1;

    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}

    ~Socket() { close(); }

    // Move
    Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = INVALID; }
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = INVALID; }
        return *this;
    }

    // Not copyable
    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    int  get()     const { return fd_; }
    bool valid()   const { return fd_ != INVALID; }

    // Release ownership without closing
    int release() { int f = fd_; fd_ = INVALID; return f; }

    // Apply a socket option (convenience)
    void set_opt(int level, int optname, int val) const {
        ::setsockopt(fd_, level, optname, &val, sizeof(val));
    }

    void close() {
        if (fd_ != INVALID) { ::close(fd_); fd_ = INVALID; }
    }

    // Set non-blocking
    void set_nonblocking() const {
        int f = ::fcntl(fd_, F_GETFL, 0);
        ::fcntl(fd_, F_SETFL, f | O_NONBLOCK);
    }

private:
    int fd_ = INVALID;
};

// ── PART 2: length-framed message protocol ────────────────────────────
// TCP is a byte stream — there are no message boundaries.  The simplest
// framing: a 4-byte big-endian length prefix before every payload.
//
//  ┌──────────────┬─────────────────────────────────┐
//  │  length (4B) │  payload (length bytes)          │
//  └──────────────┴─────────────────────────────────┘
//
// All reads loop until the exact number of bytes has been received.
// All writes loop until the entire buffer has been sent.

static bool send_frame(int fd, const void* data, uint32_t len)
{
    uint32_t net_len = htonl(len);
    // Send length prefix
    const auto* p = reinterpret_cast<const char*>(&net_len);
    std::size_t rem = 4;
    while (rem > 0) {
        ssize_t n = ::send(fd, p, rem, 0);
        if (n <= 0) return false;
        p += n; rem -= static_cast<std::size_t>(n);
    }
    // Send payload
    p = static_cast<const char*>(data);
    rem = len;
    while (rem > 0) {
        ssize_t n = ::send(fd, p, rem, 0);
        if (n <= 0) return false;
        p += n; rem -= static_cast<std::size_t>(n);
    }
    return true;
}

static std::optional<std::string> recv_frame(int fd)
{
    // Read 4-byte length
    uint32_t net_len = 0;
    auto* p  = reinterpret_cast<char*>(&net_len);
    std::size_t rem = 4;
    while (rem > 0) {
        ssize_t n = ::recv(fd, p, rem, 0);
        if (n <= 0) return std::nullopt;
        p += n; rem -= static_cast<std::size_t>(n);
    }
    uint32_t len = ntohl(net_len);
    if (len == 0 || len > 1 << 20) return std::nullopt; // sanity

    // Read payload
    std::string msg(len, '\0');
    p = msg.data(); rem = len;
    while (rem > 0) {
        ssize_t n = ::recv(fd, p, rem, 0);
        if (n <= 0) return std::nullopt;
        p += n; rem -= static_cast<std::size_t>(n);
    }
    return msg;
}

static constexpr int FRAME_PORT = 19940;

static void framed_server()
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(FRAME_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    ::listen(srv, 4);

    int cli = ::accept(srv, nullptr, nullptr);
    std::puts("framed server: accepted");

    for (;;) {
        auto msg = recv_frame(cli);
        if (!msg) break;
        std::printf("  framed server: received '%s'\n", msg->c_str());
        send_frame(cli, msg->data(), static_cast<uint32_t>(msg->size()));
    }

    ::close(cli);
    ::close(srv);
}

static void framed_client()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(30ms);

    Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
    sock.set_opt(IPPROTO_TCP, TCP_NODELAY, 1);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(FRAME_PORT);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    ::connect(sock.get(), reinterpret_cast<sockaddr*>(&a), sizeof(a));

    const char* orders[] = {
        "NEW ORDER BUY AAPL 100 @ 182.50",
        "NEW ORDER SELL MSFT 50 @ 415.20",
        "CANCEL 9876543",
    };
    for (const char* o : orders) {
        send_frame(sock.get(), o, static_cast<uint32_t>(std::strlen(o)));
        if (auto reply = recv_frame(sock.get()))
            std::printf("  framed client: echo '%s'\n", reply->c_str());
    }
    // Socket closed by RAII when sock goes out of scope
}

// ── PART 3: non-blocking connect with timeout ─────────────────────────
// Use O_NONBLOCK + poll() to implement a connect() that gives up after
// a deadline.  Essential for connecting to potentially unreachable hosts.
static std::optional<Socket> connect_with_timeout(
    const char* ip, int port, std::chrono::milliseconds timeout)
{
    Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
    sock.set_opt(IPPROTO_TCP, TCP_NODELAY, 1);
    sock.set_nonblocking();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return std::nullopt;

    int rc = ::connect(sock.get(),
                       reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) return sock; // immediate (unlikely for remote)
    if (errno != EINPROGRESS) return std::nullopt;

    // Poll for writability = connect completed
    pollfd pfd{ .fd = sock.get(), .events = POLLOUT, .revents = 0 };
    int n = ::poll(&pfd, 1, static_cast<int>(timeout.count()));

    if (n <= 0) return std::nullopt; // timeout or error

    int err = 0; socklen_t errlen = sizeof(err);
    ::getsockopt(sock.get(), SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) return std::nullopt;

    // Restore blocking mode for normal use
    int flags = ::fcntl(sock.get(), F_GETFL, 0);
    ::fcntl(sock.get(), F_SETFL, flags & ~O_NONBLOCK);

    return sock;
}

// ── PART 4: exponential backoff reconnect ────────────────────────────
// After a connection is dropped, retry with increasing delays to avoid
// hammering a recovering server.
static Socket reconnect_with_backoff(
    const char* ip, int port,
    int max_attempts = 5,
    std::chrono::milliseconds initial_delay = MS(10),
    double factor = 2.0)
{
    auto delay = initial_delay;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        std::printf("  reconnect: attempt %d, timeout=%lld ms\n",
                    attempt, static_cast<long long>(delay.count()));
        auto s = connect_with_timeout(ip, port, delay);
        if (s) {
            std::printf("  reconnect: connected on attempt %d\n", attempt);
            return std::move(*s);
        }
        if (attempt < max_attempts)
            std::this_thread::sleep_for(delay);
        delay = MS(static_cast<long long>(delay.count() * factor));
    }
    std::puts("  reconnect: all attempts failed");
    return Socket{}; // invalid
}

// ── PART 5: simple connection pool ───────────────────────────────────
// Maintains a fixed set of persistent TCP connections.
// `acquire()` returns an available socket; `release()` returns it.
// In production this would be thread-safe with a mutex + condition variable.
class ConnectionPool {
public:
    ConnectionPool(const char* host, int port, int size)
        : host_(host), port_(port)
    {
        pool_.reserve(static_cast<std::size_t>(size));
        available_.reserve(static_cast<std::size_t>(size));

        for (int i = 0; i < size; ++i) {
            auto s = connect_with_timeout(host, port, MS(200));
            if (s) {
                available_.push_back(static_cast<int>(pool_.size()));
                pool_.push_back(std::move(*s));
            }
        }
        std::printf("  pool: %zu/%d connections established to %s:%d\n",
                    pool_.size(), size, host, port);
    }

    // Returns fd of an available connection, or -1 if none
    int acquire() {
        if (available_.empty()) return -1;
        int idx = available_.back();
        available_.pop_back();
        return pool_[static_cast<std::size_t>(idx)].get();
    }

    // Return a connection to the pool
    void release(int fd) {
        for (std::size_t i = 0; i < pool_.size(); ++i) {
            if (pool_[i].get() == fd) {
                available_.push_back(static_cast<int>(i));
                return;
            }
        }
    }

    std::size_t available_count() const { return available_.size(); }

private:
    std::string         host_;
    int                 port_;
    std::vector<Socket> pool_;
    std::vector<int>    available_; // indices into pool_
};

int main()
{
    std::puts("── socket patterns demo ─────────────────────────────────");

    // ── RAII + framed protocol ────────────────────────────────────────
    std::puts("\n── length-framed protocol ───────────────────────────────");
    std::thread server_thread(framed_server);
    framed_client();
    server_thread.join();

    // ── Non-blocking connect with timeout ─────────────────────────────
    std::puts("\n── non-blocking connect with timeout ────────────────────");
    {
        // Connect to ourselves (should succeed quickly)
        // First start a tiny listener
        int echo_srv = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(echo_srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons(19941);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(echo_srv, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::listen(echo_srv, 4);

        auto sock = connect_with_timeout("127.0.0.1", 19941, MS(500));
        if (sock)
            std::puts("  connected successfully within timeout");
        else
            std::puts("  connection failed (timeout)");

        // Connect to a non-routable address to demonstrate timeout
        auto s2 = connect_with_timeout("192.0.2.1", 9999, MS(200));
        if (!s2)
            std::puts("  192.0.2.1:9999 timed out as expected");

        int c = ::accept(echo_srv, nullptr, nullptr);
        if (c >= 0) ::close(c);
        ::close(echo_srv);
    }

    // ── Exponential backoff (connect to a closed port) ────────────────
    std::puts("\n── exponential backoff reconnect ────────────────────────");
    {
        // Nothing listening — all attempts will fail, showing backoff
        Socket s = reconnect_with_backoff("127.0.0.1", 19999,
                                          4, MS(20), 2.0);
        if (!s.valid())
            std::puts("  expected: all attempts exhausted");
    }

    // ── Connection pool ───────────────────────────────────────────────
    std::puts("\n── connection pool ──────────────────────────────────────");
    {
        // Start a simple echo server that accepts multiple connections
        int pool_srv = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(pool_srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        ::setsockopt(pool_srv, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons(19942);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(pool_srv, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::listen(pool_srv, 16);

        // Accept connections in background
        std::thread acceptor([pool_srv]{
            for (int i = 0; i < 3; ++i) {
                int c = ::accept(pool_srv, nullptr, nullptr);
                if (c >= 0) {
                    // Just hold the connection open
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(300ms);
                    ::close(c);
                }
            }
            ::close(pool_srv);
        });

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(20ms);

        ConnectionPool pool("127.0.0.1", 19942, 3);
        std::printf("  pool available: %zu\n", pool.available_count());

        int fd1 = pool.acquire();
        int fd2 = pool.acquire();
        std::printf("  acquired fd1=%d  fd2=%d  remaining=%zu\n",
                    fd1, fd2, pool.available_count());

        pool.release(fd1);
        std::printf("  released fd1, available=%zu\n",
                    pool.available_count());

        acceptor.join();
    }

    std::puts("\n── done ─────────────────────────────────────────────────");
}
