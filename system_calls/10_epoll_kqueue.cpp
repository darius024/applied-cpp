#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef __linux__
#  include <sys/epoll.h>
#else
#  include <sys/event.h>  // kqueue / kevent (macOS, BSD)
#  include <sys/time.h>
#endif

// ─────────────────────────────────────────────────────────────────────
// Scalable I/O event notification: kqueue (macOS/BSD) and epoll (Linux).
//
// Both solve the same problem as poll() but at O(1) cost per event
// instead of O(n) cost per call.  They maintain an interest set inside
// the kernel and return ONLY the fds that are ready.
//
// ── kqueue (macOS / BSD) ─────────────────────────────────────────────
//   int kq = kqueue();
//     Creates a kernel event queue; returns a fd.
//
//   EV_SET(&kev, ident, filter, flags, fflags, data, udata)
//     Fills a struct kevent.
//     ident  — fd (for EVFILT_READ/WRITE) or signal number (EVFILT_SIGNAL)
//     filter — EVFILT_READ | EVFILT_WRITE | EVFILT_TIMER | EVFILT_SIGNAL
//     flags  — EV_ADD | EV_DELETE | EV_ONESHOT | EV_CLEAR | EV_ENABLE
//     fflags — filter-specific; 0 for READ/WRITE
//     data   — filter-specific: bytes available (EVFILT_READ), etc.
//     udata  — user pointer returned with the event
//
//   int n = kevent(kq, changelist, nchanges, eventlist, nevents, timeout)
//     Atomically registers changes (changelist) AND waits for events
//     (eventlist) in a single syscall.  timeout == nullptr blocks forever.
//
// ── epoll (Linux) ────────────────────────────────────────────────────
//   int epfd = epoll_create1(EPOLL_CLOEXEC);
//
//   struct epoll_event ev;
//   ev.events   = EPOLLIN | EPOLLET;  // EPOLLET = edge-triggered
//   ev.data.fd  = fd;
//   epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);  // register
//   epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);  // modify
//   epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr); // remove
//
//   int n = epoll_wait(epfd, events, maxevents, timeout_ms);
//
// ── Edge-triggered (ET) vs Level-triggered (LT) ─────────────────────
//   Level-triggered (default): fires whenever data is available.
//     Read all or leave it; the next epoll_wait will fire again.
//   Edge-triggered (EPOLLET): fires only when NEW data arrives.
//     You MUST drain the fd completely (until EAGAIN) each time.
//     One missed byte = no future event.  Used for very high throughput.
//
//   kqueue is always edge-triggered by default.
//   Use EV_CLEAR to reset the edge on kqueue.
//
// Compile:  g++ -std=c++20 10_epoll_kqueue.cpp -o epoll_kqueue && ./epoll_kqueue
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

// Start a child that writes N ticks to its end of a socketpair, then exits.
static pid_t start_feed(int sv[2], int feed_id, int n_ticks)
{
    pid_t pid = ::fork();
    if (pid == -1) die("fork feed");
    if (pid == 0) {
        ::close(sv[0]);
        for (int i = 0; i < n_ticks; ++i) {
            char msg[64];
            int  len = std::snprintf(msg, sizeof(msg),
                "FEED%d T%d px=%.2f\n", feed_id, i,
                100.0 + feed_id * 5 + i * 0.1);
            write_all(sv[1], msg, static_cast<std::size_t>(len));
            ::usleep(static_cast<unsigned>(feed_id) * 2000u);
        }
        ::close(sv[1]);
        std::exit(0);
    }
    ::close(sv[1]);
    return pid;
}

// ─────────────────────────────────────────────────────────────────────
#ifdef __linux__

static void run_event_loop(int* fds, pid_t* pids, int n)
{
    std::puts("── epoll event loop (Linux) ─────────────────────");

    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) die("epoll_create1");

    // Register all fds as level-triggered EPOLLIN.
    for (int i = 0; i < n; ++i) {
        // Set non-blocking to allow draining without blocking.
        int fl = ::fcntl(fds[i], F_GETFL);
        ::fcntl(fds[i], F_SETFL, fl | O_NONBLOCK);

        struct epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLRDHUP; // EPOLLRDHUP = peer half-closed
        ev.data.fd = fds[i];
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev) == -1)
            die("epoll_ctl ADD");
    }

    std::vector<struct epoll_event> events(static_cast<std::size_t>(n));
    int open_count = n;

    while (open_count > 0) {
        int ready = ::epoll_wait(epfd, events.data(),
                                 static_cast<int>(events.size()), 300);
        if (ready == -1) { if (errno == EINTR) continue; die("epoll_wait"); }
        if (ready == 0)  { std::puts("  epoll timeout"); continue; }

        for (int i = 0; i < ready; ++i) {
            int fd = events[static_cast<std::size_t>(i)].data.fd;
            uint32_t ev = events[static_cast<std::size_t>(i)].events;

            if (ev & EPOLLIN) {
                char buf[256]{};
                ssize_t nr;
                while ((nr = ::read(fd, buf, sizeof(buf) - 1)) > 0) {
                    buf[nr] = '\0';
                    std::printf("  [fd %d] %s", fd, buf);
                }
            }

            if (ev & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
                std::printf("  [fd %d] closed\n", fd);
                ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                ::close(fd);
                --open_count;
                // Reap the matching child.
                for (int j = 0; j < n; ++j)
                    if (fds[j] == fd) { int st; ::waitpid(pids[j], &st, 0); }
            }
        }
    }
    ::close(epfd);
}

#else // macOS / BSD

static void run_event_loop(int* fds, pid_t* pids, int n)
{
    std::puts("── kqueue event loop (macOS/BSD) ────────────────");

    int kq = ::kqueue();
    if (kq == -1) die("kqueue");

    // Register all fds with EVFILT_READ.
    // EV_SET fills a struct kevent.  We pass the index i as udata so we
    // can look up pids[i] when the fd closes.
    std::vector<struct kevent> changes(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        int fl = ::fcntl(fds[i], F_GETFL);
        ::fcntl(fds[i], F_SETFL, fl | O_NONBLOCK);
        EV_SET(&changes[static_cast<std::size_t>(i)],
               static_cast<uintptr_t>(fds[i]),
               EVFILT_READ,
               EV_ADD | EV_ENABLE,
               0, 0,
               reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }

    // Register all events in a single kevent() call (nchanges = n, nevents = 0).
    if (::kevent(kq, changes.data(), n, nullptr, 0, nullptr) == -1)
        die("kevent register");

    std::vector<struct kevent> events(static_cast<std::size_t>(n * 2));
    int open_count = n;
    struct timespec timeout{ 0, 300'000'000L }; // 300 ms

    while (open_count > 0) {
        int ready = ::kevent(kq, nullptr, 0,
                             events.data(),
                             static_cast<int>(events.size()),
                             &timeout);
        if (ready == -1) { if (errno == EINTR) continue; die("kevent wait"); }
        if (ready == 0)  { std::puts("  kqueue timeout"); continue; }

        for (int i = 0; i < ready; ++i) {
            const auto& ke = events[static_cast<std::size_t>(i)];
            int fd  = static_cast<int>(ke.ident);
            int idx = static_cast<int>(reinterpret_cast<intptr_t>(ke.udata));

            if (ke.flags & EV_ERROR) {
                std::printf("  [fd %d] EV_ERROR: %s\n", fd,
                            ::strerror(static_cast<int>(ke.data)));
                ::close(fd);
                --open_count;
                continue;
            }

            // Read all available data.
            char buf[256]{};
            ssize_t nr;
            while ((nr = ::read(fd, buf, sizeof(buf) - 1)) > 0) {
                buf[nr] = '\0';
                std::printf("  [fd %d] %s", fd, buf);
            }

            // EV_EOF means the write end was closed.
            if (ke.flags & EV_EOF) {
                std::printf("  [fd %d] EV_EOF — feed closed\n", fd);
                // Remove from kqueue before closing.
                struct kevent del;
                EV_SET(&del, static_cast<uintptr_t>(fd),
                       EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                ::kevent(kq, &del, 1, nullptr, 0, nullptr);
                ::close(fd);
                --open_count;
                int st;
                ::waitpid(pids[idx], &st, 0);
            }
        }
    }
    ::close(kq);
}

#endif // __linux__

int main()
{
    const int N = 3;
    int   fds[N];
    pid_t pids[N];

    for (int i = 0; i < N; ++i) {
        int sv[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
            die("socketpair");
        fds[i]  = sv[0];
        pids[i] = start_feed(sv, i + 1, 4);
    }

    run_event_loop(fds, pids, N);

    std::puts("\n── event loop complete ───────────────────────────");

#ifdef __linux__
    // ── Edge-triggered demo (EPOLLET) — Linux only ────────────────────
    // With ET you MUST drain until EAGAIN each time an event fires.
    std::puts("\n── EPOLLET edge-triggered demo ──────────────────");
    int et_sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, et_sv) == -1) die("socketpair ET");

    int et_epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (et_epfd == -1) die("epoll ET");

    int fl = ::fcntl(et_sv[0], F_GETFL);
    ::fcntl(et_sv[0], F_SETFL, fl | O_NONBLOCK);

    struct epoll_event et_ev{};
    et_ev.events  = EPOLLIN | EPOLLET;
    et_ev.data.fd = et_sv[0];
    ::epoll_ctl(et_epfd, EPOLL_CTL_ADD, et_sv[0], &et_ev);

    // Write two bursts to the same fd — ET fires once per burst.
    ::write(et_sv[1], "burst1", 6);
    ::write(et_sv[1], "burst2", 6);

    struct epoll_event ready_ev[4];
    int n_ready = ::epoll_wait(et_epfd, ready_ev, 4, 100);
    std::printf("  ET: epoll_wait returned %d event(s)\n", n_ready);
    // Drain completely on the one event.
    char drain_buf[32]{};
    ssize_t nr;
    while ((nr = ::read(et_sv[0], drain_buf, sizeof(drain_buf) - 1)) > 0) {
        drain_buf[nr] = '\0';
        std::printf("  ET drained: %s\n", drain_buf);
    }
    ::close(et_sv[0]); ::close(et_sv[1]); ::close(et_epfd);
#endif
}
