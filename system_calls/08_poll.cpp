#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// poll(): I/O multiplexing — wait for events on multiple file descriptors.
//
// poll(fds, nfds, timeout_ms)
//   fds     — array of struct pollfd
//   nfds    — number of elements
//   timeout — -1 block forever; 0 return immediately; >0 milliseconds
//   returns — number of fds with events; 0 on timeout; -1 on error
//
// struct pollfd:
//   int   fd       — file descriptor to watch
//   short events   — events you want: POLLIN | POLLOUT | POLLPRI
//   short revents  — events that occurred (filled by the kernel)
//
// Common revents flags:
//   POLLIN    — data is available to read (or connection arrived)
//   POLLOUT   — write space available (non-blocking write won't block)
//   POLLHUP   — peer closed its end (pipe, socket); read returns 0
//   POLLERR   — error condition on fd (returned even if not requested)
//   POLLNVAL  — fd is not open (programming bug)
//   POLLPRI   — urgent/out-of-band data (TCP urgent, Linux eventfd)
//
// Non-blocking I/O (O_NONBLOCK):
//   read/write return -1 with errno == EAGAIN / EWOULDBLOCK when not
//   ready instead of blocking. Use O_NONBLOCK on all fds passed to poll
//   to avoid unexpected blocking after poll returns.
//
// poll vs alternatives:
//   select  — max 1024 fds; fd_set manipulation; obsolete API
//   poll    — no fd count limit; clear events/revents split; portable
//   epoll   — O(1) ready notification; Linux only; see 10_epoll_kqueue.cpp
//   kqueue  — macOS/BSD equivalent to epoll; see 10_epoll_kqueue.cpp
//
// Scenario: three "market data feeds" modelled as UNIX socket pairs.
//   The event loop multiplexes reads across all feeds without blocking.
//
// Compile:  g++ -std=c++20 08_poll.cpp -o poll_demo && ./poll_demo
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

static void write_all(int fd, const void* buf, std::size_t len)
{
    const auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n <= 0) { if (errno == EINTR) continue; die("write_all"); }
        p += n; len -= static_cast<std::size_t>(n);
    }
}

// Start a "feed process" that sends ticks with delays, then closes.
// feed_sv[0] is the server (parent); feed_sv[1] is used by the child.
static pid_t start_feed(int feed_sv[2], int feed_id)
{
    pid_t pid = ::fork();
    if (pid == -1) die("fork feed");

    if (pid == 0) {
        ::close(feed_sv[0]); // child uses feed_sv[1]
        for (int i = 0; i < 4; ++i) {
            char msg[64];
            int  len = std::snprintf(msg, sizeof(msg),
                "FEED%d TICK%d price=%.2f\n",
                feed_id, i, 100.0 + feed_id * 10 + i * 0.25);
            write_all(feed_sv[1], msg, static_cast<std::size_t>(len));
            ::usleep(static_cast<unsigned>(feed_id + 1) * 3000u); // staggered
        }
        ::close(feed_sv[1]);
        std::exit(0);
    }

    ::close(feed_sv[1]); // parent uses feed_sv[0]
    return pid;
}

// ── Main poll event loop ──────────────────────────────────────────────
static void event_loop(int* reader_fds, int n_feeds, pid_t* pids)
{
    std::vector<struct pollfd> pfds(static_cast<std::size_t>(n_feeds));
    for (int i = 0; i < n_feeds; ++i) {
        pfds[static_cast<std::size_t>(i)] = { reader_fds[i], POLLIN, 0 };
        // Set non-blocking so reads after POLLIN don't accidentally block.
        int flags = ::fcntl(reader_fds[i], F_GETFL);
        ::fcntl(reader_fds[i], F_SETFL, flags | O_NONBLOCK);
    }

    int open_fds = n_feeds;
    while (open_fds > 0) {
        int ready = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 200);
        if (ready == -1) { if (errno == EINTR) continue; die("poll"); }
        if (ready == 0) {
            std::puts("  poll timeout (200 ms) — no events");
            continue;
        }

        for (int i = 0; i < n_feeds; ++i) {
            auto& pfd = pfds[static_cast<std::size_t>(i)];
            if (pfd.fd == -1) continue; // already closed

            if (pfd.revents & POLLIN) {
                char buf[256]{};
                ssize_t n;
                // Drain all available data (non-blocking read loop).
                while ((n = ::read(pfd.fd, buf, sizeof(buf) - 1)) > 0) {
                    buf[n] = '\0';
                    std::printf("  [fd %d] %s", pfd.fd, buf);
                }
                if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
                    die("read event");
            }

            if (pfd.revents & POLLHUP) {
                // Peer closed its end of the socket (feed process exited).
                std::printf("  [fd %d] POLLHUP — feed closed\n", pfd.fd);
                ::close(pfd.fd);
                pfd.fd = -1; // tell poll to ignore this slot
                --open_fds;
                int status;
                ::waitpid(pids[i], &status, 0);
            }

            if (pfd.revents & POLLERR) {
                std::printf("  [fd %d] POLLERR\n", pfd.fd);
                ::close(pfd.fd);
                pfd.fd = -1;
                --open_fds;
            }

            pfd.revents = 0; // clear for next iteration
        }
    }
}

int main()
{
    std::puts("── poll multiplexer (3 market data feeds) ───────");

    const int N = 3;
    int    reader_fds[N];
    pid_t  pids[N];

    // Create a socketpair per feed; start a child that writes into it.
    for (int i = 0; i < N; ++i) {
        int sv[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
            die("socketpair");
        reader_fds[i] = sv[0];
        pids[i]       = start_feed(sv, i + 1);
    }

    event_loop(reader_fds, N, pids);

    std::puts("\n── poll done ─────────────────────────────────────");

    // ── POLLOUT demo: write readiness ─────────────────────────────────
    // Write-side polling is used for non-blocking network sends: you only
    // write when POLLOUT fires to avoid blocking when the kernel buffer
    // is full.  Demonstrated here with a self-pipe.
    std::puts("\n── POLLOUT demo (write-readiness check) ──────────");
    int pout[2];
    if (::pipe2(pout, O_NONBLOCK | O_CLOEXEC) == -1) die("pipe2 pollout");

    struct pollfd wfd = { pout[1], POLLOUT, 0 };
    int r = ::poll(&wfd, 1, 100);
    if (r > 0 && (wfd.revents & POLLOUT)) {
        const char* data = "ready to write";
        write_all(pout[1], data, std::strlen(data));
        std::puts("  POLLOUT fired — wrote without blocking");
    }
    ::close(pout[0]);
    ::close(pout[1]);
}
