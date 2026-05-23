#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// io_uring: Linux's asynchronous I/O interface (Linux 5.1+).
//
// io_uring uses two ring buffers shared between user space and the kernel:
//
//   Submission Queue (SQ): user fills SQEs (Submission Queue Entries)
//     with operations (accept, recv, send, read, write, …) and advances
//     the tail pointer. No syscall needed to enqueue.
//
//   Completion Queue (CQ): the kernel fills CQEs (Completion Queue Entries)
//     when operations finish, advancing the tail. User polls or waits with
//     io_uring_enter() to harvest completions.
//
// Key modes:
//   Default: io_uring_enter() syscall to submit and/or wait for events.
//   SQPOLL (IORING_SETUP_SQPOLL): kernel spawns a thread that polls
//     the SQ continuously — zero syscalls for submission when it's active.
//   Fixed buffers (io_uring_register_buffers): pre-register user buffers
//     to avoid per-operation address translation.
//
// This file uses the liburing C wrapper (https://github.com/axboe/liburing).
// On systems without io_uring the code prints a build note.
//
// Build (Linux with liburing):
//   g++ -std=c++20 09_io_uring.cpp -luring -o io_uring_demo && ./io_uring_demo
// ─────────────────────────────────────────────────────────────────────

#ifdef __linux__
// Try to include liburing.  If absent, we fall through to the stub.
#  if __has_include(<liburing.h>)
#    include <liburing.h>
#    define HAVE_LIBURING 1
#  endif
#endif

#ifdef HAVE_LIBURING

#include <thread>
#include <chrono>
#include <cstdint>
#include <string>

static constexpr int PORT    = 19930;
static constexpr int BACKLOG = 16;
static constexpr int QD      = 64;   // queue depth (power of 2)

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── User-data tags to distinguish operation types in the CQ ──────────
enum class OpTag : uint64_t {
    Accept = 1,
    Recv   = 2,
    Send   = 3,
};

struct ConnCtx {
    int     fd;
    char    buf[1024];
    ssize_t buf_len;
};

// ── io_uring echo server ──────────────────────────────────────────────
// Lifecycle:
//   1. Submit an SQE for accept() on the server socket.
//   2. On accept CQE: submit an SQE for recv() on the client socket.
//   3. On recv CQE: if data arrived, submit an SQE for send() (echo).
//   4. On send CQE: submit another recv() to continue.
//   5. If recv returns 0 (EOF), close the client fd.
static void io_uring_echo_server()
{
    // ── Create server socket ──────────────────────────────────────────
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv == -1) die("socket");
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, BACKLOG);
    std::puts("io_uring server: listening");

    // ── Initialise io_uring ───────────────────────────────────────────
    io_uring ring{};
    io_uring_params params{};
    // IORING_SETUP_SQPOLL would require root or CAP_SYS_ADMIN; skip here.
    if (::io_uring_queue_init_params(QD, &ring, &params) < 0)
        die("io_uring_queue_init_params");

    // Probe which operations are available
    io_uring_probe* probe = ::io_uring_get_probe_ring(&ring);
    auto op_ok = [&](int op) {
        return probe && ::io_uring_opcode_supported(probe, op);
    };
    std::printf("  IORING_OP_ACCEPT supported: %s\n",
                op_ok(IORING_OP_ACCEPT) ? "yes" : "no");
    std::printf("  IORING_OP_RECV   supported: %s\n",
                op_ok(IORING_OP_RECV)   ? "yes" : "no");
    std::printf("  IORING_OP_SEND   supported: %s\n",
                op_ok(IORING_OP_SEND)   ? "yes" : "no");
    if (probe) ::free(probe);

    // ── Submit initial accept SQE ─────────────────────────────────────
    sockaddr_in client_addr{};
    socklen_t   client_len = sizeof(client_addr);

    {
        io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
        ::io_uring_prep_accept(sqe, srv,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len, 0);
        ::io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(OpTag::Accept));
        ::io_uring_submit(&ring);
    }

    ConnCtx ctx{};
    int     client_fd = -1;

    // ── Event loop ────────────────────────────────────────────────────
    for (int iterations = 0; iterations < 20; ++iterations) {
        io_uring_cqe* cqe = nullptr;

        // Wait for one completion (blocks until an op finishes)
        int r = ::io_uring_wait_cqe(&ring, &cqe);
        if (r < 0) {
            std::fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-r));
            break;
        }

        auto tag = static_cast<OpTag>(::io_uring_cqe_get_data64(cqe));
        int  res = cqe->res;
        ::io_uring_cqe_seen(&ring, cqe); // mark consumed

        switch (tag) {
        case OpTag::Accept:
            if (res < 0) {
                std::fprintf(stderr, "accept failed: %s\n", strerror(-res));
                goto done;
            }
            client_fd  = res;
            ctx.fd     = client_fd;
            std::printf("  io_uring: accepted fd=%d\n", client_fd);

            // Submit recv SQE for the new client
            {
                io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
                ::io_uring_prep_recv(sqe, client_fd,
                                     ctx.buf, sizeof(ctx.buf) - 1, 0);
                ::io_uring_sqe_set_data64(sqe,
                    static_cast<uint64_t>(OpTag::Recv));
                ::io_uring_submit(&ring);
            }
            break;

        case OpTag::Recv:
            if (res <= 0) {
                std::printf("  io_uring: client fd=%d closed (res=%d)\n",
                            client_fd, res);
                ::close(client_fd);
                goto done;
            }
            ctx.buf_len   = res;
            ctx.buf[res]  = '\0';
            std::printf("  io_uring: recv %d bytes: '%s'\n", res, ctx.buf);

            // Echo: submit send SQE
            {
                io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
                ::io_uring_prep_send(sqe, client_fd,
                                     ctx.buf, static_cast<std::size_t>(res), 0);
                ::io_uring_sqe_set_data64(sqe,
                    static_cast<uint64_t>(OpTag::Send));
                ::io_uring_submit(&ring);
            }
            break;

        case OpTag::Send:
            if (res < 0) {
                std::fprintf(stderr, "send failed: %s\n", strerror(-res));
                ::close(client_fd);
                goto done;
            }
            std::printf("  io_uring: sent %d bytes\n", res);

            // Submit next recv
            {
                io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
                ::io_uring_prep_recv(sqe, client_fd,
                                     ctx.buf, sizeof(ctx.buf) - 1, 0);
                ::io_uring_sqe_set_data64(sqe,
                    static_cast<uint64_t>(OpTag::Recv));
                ::io_uring_submit(&ring);
            }
            break;
        }
    }

done:
    ::io_uring_queue_exit(&ring);
    ::close(srv);
    std::puts("io_uring server: done");
}

static void tcp_client()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    const char* msgs[] = { "QUOTE AAPL 182.50", "TRADE MSFT 100", "BYE" };
    for (const char* msg : msgs) {
        ::send(fd, msg, std::strlen(msg), 0);
        char reply[256]{};
        ssize_t n = ::recv(fd, reply, sizeof(reply) - 1, 0);
        if (n > 0) {
            reply[n] = '\0';
            std::printf("  client echo: '%s'\n", reply);
        }
        std::this_thread::sleep_for(5ms);
    }

    ::close(fd);
}

// ── io_uring linked operations example ───────────────────────────────
// Multiple SQEs can be chained with IOSQE_IO_LINK: the second starts
// only after the first completes successfully.
static void demo_linked_ops()
{
    std::puts("\n── io_uring linked SQEs concept ─────────────────────────");
    std::puts("  SQE with IOSQE_IO_LINK: next SQE runs after this one");
    std::puts("  Example: recv → send chained (no intermediate wake-up)");
    std::puts("  io_uring_prep_recv(sqe1, fd, buf, len, 0);");
    std::puts("  sqe1->flags |= IOSQE_IO_LINK;");
    std::puts("  io_uring_prep_send(sqe2, fd, buf, len, 0);");
    std::puts("  io_uring_sqe_set_data(sqe2, ptr);");
    std::puts("  io_uring_submit(&ring); // both submitted atomically");
    std::puts("  Only one CQE wakes the user (for sqe2), if sqe1 succeeded.");
}

int main()
{
    std::puts("── io_uring demo ────────────────────────────────────────");

    std::thread server_thread(io_uring_echo_server);
    tcp_client();
    server_thread.join();

    demo_linked_ops();

    std::puts("\n── done ─────────────────────────────────────────────────");
}

#else // !HAVE_LIBURING

int main()
{
    std::puts("── io_uring demo ────────────────────────────────────────");

#ifndef __linux__
    std::puts("io_uring is Linux-only.");
    std::puts("On macOS, use kqueue (see 06_select_poll_epoll.cpp).");
#else
    std::puts("liburing not found.  Install it:");
    std::puts("  apt install liburing-dev   # Debian/Ubuntu");
    std::puts("  dnf install liburing-devel # Fedora/RHEL");
    std::puts("Then rebuild:");
    std::puts("  g++ -std=c++20 09_io_uring.cpp -luring -o io_uring_demo");
#endif

    std::puts("\nKey concepts:");
    std::puts("  SQ (submission queue): user writes SQEs (ops), advances tail");
    std::puts("  CQ (completion queue): kernel writes CQEs (results), user reads");
    std::puts("  IORING_SETUP_SQPOLL:   kernel polls SQ; zero syscalls on hot path");
    std::puts("  Fixed buffers:         pre-registered bufs avoid per-op DMA mapping");
    std::puts("  Linked SQEs:           chain ops without intermediate wake-ups");
    std::puts("  Multishot accept:      one SQE accepts many connections");
    std::puts("  Provided buffers:      kernel picks from a pool, avoids re-arm");
}

#endif // HAVE_LIBURING
