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
#include <thread>

// ─────────────────────────────────────────────────────────────────────
// Non-blocking I/O: O_NONBLOCK, EAGAIN, EINPROGRESS.
// poll() for readiness on multiple file descriptors.
//
// By default every socket syscall (recv, send, connect, accept) blocks:
// the calling thread sleeps inside the kernel until data is available or
// the operation completes.  That is fine for a thread-per-connection
// model but doesn't scale beyond a few thousand connections.
//
// O_NONBLOCK changes all those operations to return immediately:
//   recv → EAGAIN if no data is ready
//   send → EAGAIN if the send buffer is full
//   connect → EINPROGRESS if the handshake is in progress
//   accept → EAGAIN if no pending connection
//
// poll() lets one thread watch multiple fds and wake up only when one
// becomes ready — avoiding busy-spinning on EAGAIN.
//
// Compile: g++ -std=c++20 03_nonblocking_io.cpp -o nb_io && ./nb_io
// ─────────────────────────────────────────────────────────────────────

static constexpr int PORT = 19902;
using Clock = std::chrono::steady_clock;
using MS    = std::chrono::milliseconds;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Make a socket non-blocking ────────────────────────────────────────
static void set_nonblocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) die("fcntl F_GETFL");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) die("fcntl F_SETFL");
}

// ── PART 1: non-blocking recv with poll ──────────────────────────────
// A small server that accepts one connection and demonstrates the recv
// loop: poll for readability, then drain until EAGAIN.
static void nonblocking_server()
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv == -1) die("socket");

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 4);

    // Accept loop using poll on the listening socket
    set_nonblocking(srv);

    std::puts("server: waiting for connection (non-blocking accept)");
    int cli = -1;
    while (cli == -1) {
        pollfd pfd{ .fd = srv, .events = POLLIN, .revents = 0 };
        int n = ::poll(&pfd, 1, 5000 /*ms*/);
        if (n == 0) { std::puts("server: poll timeout"); continue; }
        if (n < 0 && errno == EINTR) continue;

        cli = ::accept(srv, nullptr, nullptr);
        if (cli == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            cli = -1; // spurious wakeup, retry
        } else if (cli == -1) {
            die("accept");
        }
    }
    std::puts("server: accepted connection");
    set_nonblocking(cli);

    // ── Non-blocking recv loop ────────────────────────────────────────
    // Use poll to wait for data, then read until EAGAIN (buffer drained).
    // This is the canonical edge-triggered pattern.
    char    buf[256];
    int     msg_count = 0;

    for (;;) {
        pollfd pfd{ .fd = cli, .events = POLLIN, .revents = 0 };
        int n = ::poll(&pfd, 1, 2000);
        if (n == 0) {
            std::puts("server: recv timeout, done");
            break;
        }

        // Drain until EAGAIN
        while (true) {
            ssize_t r = ::recv(cli, buf, sizeof(buf) - 1, 0);
            if (r == 0) {
                std::puts("server: EOF");
                goto done;
            }
            if (r == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // drained
                die("recv");
            }
            buf[r] = '\0';
            std::printf("server: received [%zd bytes] '%s'\n", r, buf);
            ++msg_count;
        }
    }
done:
    std::printf("server: total messages received: %d\n", msg_count);
    ::close(cli);
    ::close(srv);
}

// ── PART 2: non-blocking connect ─────────────────────────────────────
// connect() on a non-blocking socket returns immediately with -1 and
// errno == EINPROGRESS.  poll() on POLLOUT tells us when the handshake
// completes.  Then getsockopt(SO_ERROR) reveals success or failure.
static void nonblocking_connect_and_send()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(40ms); // let server start

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    set_nonblocking(fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        std::puts("client: connected immediately (unlikely on non-blocking)");
    } else if (errno == EINPROGRESS) {
        std::puts("client: connect in progress (EINPROGRESS), polling...");

        // Wait for writability — signals connect completion
        pollfd pfd{ .fd = fd, .events = POLLOUT, .revents = 0 };
        int n = ::poll(&pfd, 1, 3000);
        if (n <= 0) die("poll for connect");

        // Check if connect succeeded
        int err = 0;
        socklen_t errlen = sizeof(err);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
        if (err != 0) {
            std::fprintf(stderr, "client: connect failed: %s\n", strerror(err));
            ::close(fd);
            return;
        }
        std::puts("client: connect completed successfully");
    } else {
        die("connect");
    }

    // ── Send messages: poll for writability before each send ──────────
    // For a non-blocking socket, send() returns EAGAIN if the kernel
    // send buffer is full.  On loopback this rarely happens for small
    // messages, but the pattern is important for production code.
    const char* msgs[] = {
        "QUOTE AAPL 182.50 182.51",
        "QUOTE MSFT  415.20 415.25",
        "TRADE AAPL 182.50 100",
    };

    for (const char* msg : msgs) {
        std::size_t remaining = std::strlen(msg);
        const char* p         = msg;

        while (remaining > 0) {
            pollfd pfd2{ .fd = fd, .events = POLLOUT, .revents = 0 };
            ::poll(&pfd2, 1, 1000);

            ssize_t sent = ::send(fd, p, remaining, 0);
            if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // retry
                die("send");
            }
            p         += sent;
            remaining -= static_cast<std::size_t>(sent);
        }
        std::printf("client: sent '%s'\n", msg);

        // Small pause so server processes each message separately
        std::this_thread::sleep_for(5ms);
    }

    ::shutdown(fd, SHUT_WR);
    ::close(fd);
    std::puts("client: done");
}

// ── PART 3: poll multiple sockets ────────────────────────────────────
// poll() watches an array of pollfd structs. The kernel fills in
// .revents for each fd that became ready. This is the foundation of
// any event-driven server.
static void demo_poll_multiple()
{
    std::puts("\n── poll() multiple socket demo ──────────────────────────");
    std::puts("  (creating two listening sockets, polling both)");

    int fd1 = ::socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = ::socket(AF_INET, SOCK_STREAM, 0);

    for (int fd : {fd1, fd2}) {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        set_nonblocking(fd);
    }

    auto bind_listen = [](int fd, int port) {
        sockaddr_in a{};
        a.sin_family      = AF_INET;
        a.sin_port        = htons(static_cast<uint16_t>(port));
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::listen(fd, 4);
    };
    bind_listen(fd1, 19903);
    bind_listen(fd2, 19904);

    // Watch both listening fds for incoming connections
    pollfd fds[2] = {
        { .fd = fd1, .events = POLLIN, .revents = 0 },
        { .fd = fd2, .events = POLLIN, .revents = 0 },
    };

    std::printf("  polling fd=%d (port 19903) and fd=%d (port 19904) "
                "with 500 ms timeout\n", fd1, fd2);

    int n = ::poll(fds, 2, 500);
    if (n == 0) {
        std::puts("  poll timed out (no connections) — as expected");
    } else {
        for (int i = 0; i < 2; ++i) {
            if (fds[i].revents & POLLIN)
                std::printf("  fd=%d is ready for accept\n", fds[i].fd);
            if (fds[i].revents & POLLERR)
                std::printf("  fd=%d has error\n", fds[i].fd);
            if (fds[i].revents & POLLHUP)
                std::printf("  fd=%d hung up\n", fds[i].fd);
        }
    }

    ::close(fd1);
    ::close(fd2);
}

int main()
{
    std::puts("── non-blocking I/O demo ────────────────────────────────");

    std::thread server_thread(nonblocking_server);
    nonblocking_connect_and_send();
    server_thread.join();

    demo_poll_multiple();

    std::puts("\n── done ─────────────────────────────────────────────────");
}
