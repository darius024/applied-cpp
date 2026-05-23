#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#  include <sys/sendfile.h>
#endif
#ifdef __APPLE__
#  include <sys/types.h>
#  include <sys/uio.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// ─────────────────────────────────────────────────────────────────────
// Zero-copy I/O: sendfile (Linux + macOS), splice (Linux).
//
// Normally sending a file requires:
//   1. read(file_fd, user_buf, n)   — kernel → user space copy
//   2. send(sock_fd, user_buf, n)   — user space → kernel socket buf copy
//
// sendfile() tells the kernel to copy directly from the page cache to
// the socket buffer — no user-space involvement, no extra copy.
// This halves CPU usage and memory bandwidth for file-serving workloads.
//
// splice() (Linux) is more general: it moves data between two kernel
// buffers via a pipe, never touching user space.
//
// MSG_ZEROCOPY (Linux 4.14+): pins user pages and DMA-maps them
// directly to the NIC.  Requires draining an error queue for
// completion notification.  Worthwhile only for large (> 10 KiB) sends.
//
// API differences:
//   Linux:  ssize_t sendfile(out_fd, in_fd, &offset, count)
//   macOS:  int     sendfile(in_fd, out_fd, offset, &len, hdtr, flags)
//
// Compile:
//   Linux:  g++ -std=c++20 08_sendfile_zerocopy.cpp -o zerocopy && ./zerocopy
//   macOS:  g++ -std=c++20 08_sendfile_zerocopy.cpp -o zerocopy && ./zerocopy
// ─────────────────────────────────────────────────────────────────────

static constexpr int   PORT       = 19920;
static constexpr const char* TMPFILE = "/tmp/applied_cpp_feed.bin";

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Create a temporary binary file to send ───────────────────────────
// Simulates a historical tick file: N 8-byte prices.
static off_t create_test_file(int n_prices)
{
    int fd = ::open(TMPFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open test file");

    for (int i = 0; i < n_prices; ++i) {
        double price = 100.0 + i * 0.01;
        ::write(fd, &price, sizeof(price));
    }
    ::close(fd);

    struct stat st{};
    ::stat(TMPFILE, &st);
    return st.st_size;
}

// ── Server: receives bytes and counts them ────────────────────────────
static void count_receiver(int expected_bytes)
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 4);

    int cli = ::accept(srv, nullptr, nullptr);

    char    buf[65536];
    ssize_t total = 0;
    while (total < expected_bytes) {
        ssize_t n = ::recv(cli, buf, sizeof(buf), 0);
        if (n <= 0) break;
        total += n;
    }
    std::printf("receiver: got %zd bytes (expected %d)\n",
                total, expected_bytes);
    ::close(cli);
    ::close(srv);
}

static int make_client()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("connect");
    return fd;
}

// ── PART 1: sendfile ──────────────────────────────────────────────────
static void demo_sendfile()
{
    std::puts("\n── sendfile demo ────────────────────────────────────────");

    const int   N      = 10000;            // 10 K prices = 80 KiB
    off_t       fsize  = create_test_file(N);
    int         expected = static_cast<int>(fsize);

    std::printf("  file size: %lld bytes\n", static_cast<long long>(fsize));

    std::thread receiver_thread([expected]{ count_receiver(expected); });
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(30ms);

    int sock_fd = make_client();
    int file_fd = ::open(TMPFILE, O_RDONLY);
    if (file_fd == -1) die("open for sendfile");

    auto t0 = Clock::now();

#ifdef __linux__
    // Linux sendfile: keeps calling until all bytes are sent.
    off_t   offset = 0;
    ssize_t remaining = fsize;
    while (remaining > 0) {
        ssize_t sent = ::sendfile(sock_fd, file_fd, &offset, static_cast<std::size_t>(remaining));
        if (sent == -1) {
            if (errno == EINTR) continue;
            die("sendfile");
        }
        remaining -= sent;
    }
#elif defined(__APPLE__)
    // macOS sendfile: &len is in/out (in=bytes to send, out=bytes sent).
    off_t offset = 0;
    off_t len    = fsize;
    while (len > 0) {
        off_t this_len = len;
        int rc = ::sendfile(file_fd, sock_fd, offset, &this_len, nullptr, 0);
        if (rc == -1 && errno != EAGAIN) die("sendfile");
        offset += this_len;
        len    -= this_len;
    }
#else
    // Fallback: read + send loop (two copies)
    char buf[65536];
    ssize_t r;
    while ((r = ::read(file_fd, buf, sizeof(buf))) > 0)
        ::send(sock_fd, buf, static_cast<std::size_t>(r), 0);
    std::puts("  (sendfile not available; used read+send fallback)");
#endif

    auto t1 = Clock::now();
    double us = static_cast<double>(
        std::chrono::duration_cast<NS>(t1 - t0).count()) / 1000.0;
    std::printf("  sendfile %lld bytes: %.1f µs\n",
                static_cast<long long>(fsize), us);

    ::close(file_fd);
    ::close(sock_fd);
    receiver_thread.join();
    ::unlink(TMPFILE);
}

// ── PART 2: splice (Linux) ────────────────────────────────────────────
// splice() moves data between kernel buffers via a pipe, avoiding any
// user-space copy.  Useful for proxying between two sockets.
#ifdef __linux__
#include <fcntl.h>  // SPLICE_F_MOVE, SPLICE_F_NONBLOCK

static void demo_splice()
{
    std::puts("\n── splice (Linux) demo ──────────────────────────────────");

    // Create a pipe as the intermediate kernel buffer
    int pipefd[2];
    if (::pipe(pipefd) == -1) die("pipe");

    const int N     = 5000;   // 5 K prices = 40 KiB
    off_t fsize     = create_test_file(N);
    int expected    = static_cast<int>(fsize);

    std::thread receiver_thread([expected]{
        // Use port+1 to avoid collision
        int srv = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(PORT + 1);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(srv, 4);
        int cli = ::accept(srv, nullptr, nullptr);
        char buf[65536]; ssize_t total = 0;
        while (total < expected) {
            ssize_t n = ::recv(cli, buf, sizeof(buf), 0);
            if (n <= 0) break;
            total += n;
        }
        std::printf("  splice receiver: got %zd bytes\n", total);
        ::close(cli); ::close(srv);
    });

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(30ms);

    int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT + 1);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(sock_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    int file_fd = ::open(TMPFILE, O_RDONLY);
    if (file_fd == -1) die("open for splice");

    auto t0 = Clock::now();

    // Step 1: splice file_fd → pipe write end
    ssize_t remaining = fsize;
    while (remaining > 0) {
        ssize_t n = ::splice(file_fd, nullptr, pipefd[1], nullptr,
                             static_cast<std::size_t>(remaining),
                             SPLICE_F_MOVE);
        if (n == -1) die("splice file→pipe");
        // Step 2: splice pipe read end → socket
        ssize_t m = ::splice(pipefd[0], nullptr, sock_fd, nullptr,
                             static_cast<std::size_t>(n),
                             SPLICE_F_MOVE);
        if (m == -1) die("splice pipe→sock");
        remaining -= m;
    }

    auto t1 = Clock::now();
    double us = static_cast<double>(
        std::chrono::duration_cast<NS>(t1 - t0).count()) / 1000.0;
    std::printf("  splice %lld bytes: %.1f µs\n",
                static_cast<long long>(fsize), us);

    ::close(file_fd);
    ::close(sock_fd);
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    ::unlink(TMPFILE);
    receiver_thread.join();
}
#endif // __linux__

// ── PART 3: MSG_ZEROCOPY (Linux 4.14+) ───────────────────────────────
// Pins user pages and DMA-maps them directly, bypassing the kernel copy
// into the socket buffer.  Only worthwhile for large (> 10 KiB) payloads
// because the completion notification overhead (recvmsg on MSG_ERRQUEUE)
// adds fixed overhead of ~2–5 µs.
#ifdef __linux__
#include <linux/errqueue.h>
#include <sys/uio.h>

static void demo_msg_zerocopy()
{
    std::puts("\n── MSG_ZEROCOPY (Linux) ─────────────────────────────────");

    // Only available if the kernel supports it and SO_ZEROCOPY is set.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)) == -1) {
        std::puts("  SO_ZEROCOPY not available on this kernel (need 4.14+)");
        ::close(fd);
        return;
    }
    std::puts("  SO_ZEROCOPY enabled");
    std::puts("  Pattern: send(fd, buf, len, MSG_ZEROCOPY)");
    std::puts("  Then drain error queue to know when buf is safe to reuse:");
    std::puts("    recvmsg(fd, &msg, MSG_ERRQUEUE)");
    std::puts("  The cmsghdr contains sock_extended_err with ee_errno=0");
    std::puts("  ee_data = last zerocopy sequence number completed");
    std::puts("  Worthwhile only for sends > 10 KiB (fixed notification overhead)");
    ::close(fd);
}
#endif // __linux__

int main()
{
    std::puts("── sendfile / zero-copy I/O demo ────────────────────────");

    demo_sendfile();

#ifdef __linux__
    demo_splice();
    demo_msg_zerocopy();
#else
    std::puts("\n(splice and MSG_ZEROCOPY are Linux-only — skipped on macOS)");
#endif

    std::puts("\n── done ─────────────────────────────────────────────────");
}
