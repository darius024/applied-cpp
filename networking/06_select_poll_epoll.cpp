#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#  include <sys/epoll.h>
#elif defined(__APPLE__)
#  include <sys/event.h>
#  include <sys/time.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Event-driven I/O: select, poll, epoll (Linux), kqueue (macOS).
//
// All three answer the question: "which of these fds is ready?"
//
//   select()   — portable but limited to FD_SETSIZE (1024) fds; O(n) scan
//   poll()     — no fd limit; O(n) scan; preferred over select
//   epoll/kqueue — O(1) per event; register once, get only ready fds back
//
// epoll (Linux) and kqueue (macOS) are the production choice:
// a server handling 100 K connections spends negligible time in
// epoll_wait even when most connections are idle.
//
// Two trigger modes (epoll):
//   Level-triggered (LT, default): returns ready as long as buffer has data.
//     Simple: read a chunk, call epoll_wait again.
//   Edge-triggered (ET, EPOLLET): fires once when data arrives.
//     Must drain the socket (loop until EAGAIN) or data is missed.
//     Faster (fewer wakeups) but requires non-blocking sockets.
//
// Compile:
//   Linux:  g++ -std=c++20 06_select_poll_epoll.cpp -o epoll_demo && ./epoll_demo
//   macOS:  g++ -std=c++20 06_select_poll_epoll.cpp -o kqueue_demo && ./kqueue_demo
// ─────────────────────────────────────────────────────────────────────

static constexpr int BASE_PORT = 19910;
using Clock = std::chrono::steady_clock;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

static void set_nonblocking(int fd)
{
    int f = ::fcntl(fd, F_GETFL, 0);
    if (f == -1) die("fcntl F_GETFL");
    if (::fcntl(fd, F_SETFL, f | O_NONBLOCK) == -1) die("fcntl F_SETFL");
}

static int make_server(int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    set_nonblocking(fd);

    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    ::listen(fd, 16);
    return fd;
}

static int make_client(int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == -1)
        die("connect");
    return fd;
}

// ── PART 1: select() ─────────────────────────────────────────────────
// select() works by passing three bit-sets (read/write/error).
// FD_SETSIZE limits the number of fds to 1024 on most systems.
// The kernel scans all bits on each call — O(n) regardless of activity.
static void demo_select()
{
    std::puts("\n── select() demo ────────────────────────────────────────");

    int srv = make_server(BASE_PORT);
    std::thread client_thread([]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(30ms);
        int c = make_client(BASE_PORT);
        ::send(c, "hello", 5, 0);
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(10ms);
        ::close(c);
    });

    // Watch the listening socket for POLLIN (incoming connection)
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(srv, &rfds);

    struct timeval tv{ .tv_sec = 2, .tv_usec = 0 };
    int n = ::select(srv + 1, &rfds, nullptr, nullptr, &tv);
    if (n <= 0) { std::puts("  select: no connection"); goto sel_done; }

    if (FD_ISSET(srv, &rfds)) {
        int cli = ::accept(srv, nullptr, nullptr);
        std::puts("  select: accept() got a connection");

        // Now select on the client fd for data
        FD_ZERO(&rfds);
        FD_SET(cli, &rfds);
        tv = { .tv_sec = 1, .tv_usec = 0 };
        n = ::select(cli + 1, &rfds, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(cli, &rfds)) {
            char buf[64]{};
            ssize_t r = ::recv(cli, buf, sizeof(buf) - 1, 0);
            std::printf("  select: received %zd bytes: '%s'\n", r, buf);
        }
        ::close(cli);
    }
sel_done:
    ::close(srv);
    client_thread.join();
}

// ── PART 2: poll() ───────────────────────────────────────────────────
// poll() uses an array of struct pollfd.  No fd limit. Still O(n)
// kernel scan.  Preferred over select for portability.
static void demo_poll()
{
    std::puts("\n── poll() demo ──────────────────────────────────────────");

    int srv = make_server(BASE_PORT + 1);
    std::thread client_thread([]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(30ms);
        int c = make_client(BASE_PORT + 1);
        for (int i = 0; i < 3; ++i) {
            char msg[32];
            std::snprintf(msg, sizeof(msg), "MSG%d", i);
            ::send(c, msg, std::strlen(msg), 0);
            std::this_thread::sleep_for(5ms);
        }
        ::close(c);
    });

    std::vector<pollfd> fds;
    fds.push_back({ .fd = srv, .events = POLLIN, .revents = 0 });

    int connections = 0;
    auto deadline = Clock::now() + std::chrono::seconds(3);

    while (Clock::now() < deadline) {
        int n = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 200);
        if (n < 0 && errno != EINTR) die("poll");
        if (n == 0) continue;

        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (!(fds[i].revents & POLLIN)) continue;

            if (fds[i].fd == srv) {
                // New connection
                int cli = ::accept(srv, nullptr, nullptr);
                if (cli == -1) continue;
                set_nonblocking(cli);
                fds.push_back({ .fd = cli, .events = POLLIN, .revents = 0 });
                std::printf("  poll: accepted fd=%d (total fds=%zu)\n",
                            cli, fds.size());
                ++connections;
            } else {
                // Data from an existing connection
                char buf[128]{};
                ssize_t r = ::recv(fds[i].fd, buf, sizeof(buf) - 1, 0);
                if (r <= 0) {
                    std::printf("  poll: fd=%d closed\n", fds[i].fd);
                    ::close(fds[i].fd);
                    fds.erase(fds.begin() + static_cast<long>(i));
                    --i;
                    if (fds.size() == 1) goto poll_done; // only server left
                } else {
                    std::printf("  poll: fd=%d data='%s'\n", fds[i].fd, buf);
                }
            }
        }
    }
poll_done:
    ::close(srv);
    client_thread.join();
}

// ── PART 3: epoll (Linux) / kqueue (macOS) ───────────────────────────
#ifdef __linux__

static void demo_epoll()
{
    std::puts("\n── epoll (Linux) edge-triggered demo ───────────────────");

    int srv = make_server(BASE_PORT + 2);

    // Create epoll instance
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) die("epoll_create1");

    // Register listening socket: level-triggered POLLIN
    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = srv;
    ::epoll_ctl(epfd, EPOLL_CTL_ADD, srv, &ev);

    std::thread client_thread([]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(30ms);
        int c = make_client(BASE_PORT + 2);
        // Send a burst so edge-trigger fires once for multiple bytes
        ::send(c, "TICK1", 5, 0);
        ::send(c, "TICK2", 5, 0);
        std::this_thread::sleep_for(20ms);
        ::close(c);
    });

    epoll_event events[16];
    int         rounds = 0;

    while (rounds < 10) {
        int n = ::epoll_wait(epfd, events, 16, 500 /*ms*/);
        if (n == 0) { std::puts("  epoll_wait timeout"); break; }
        if (n < 0 && errno == EINTR) continue;
        ++rounds;

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == srv) {
                // Accept and register with EPOLLIN | EPOLLET (edge-triggered)
                int cli = ::accept(srv, nullptr, nullptr);
                if (cli == -1) continue;
                set_nonblocking(cli);

                epoll_event cev{};
                cev.events  = EPOLLIN | EPOLLET; // edge-triggered
                cev.data.fd = cli;
                ::epoll_ctl(epfd, EPOLL_CTL_ADD, cli, &cev);
                std::printf("  epoll: accepted fd=%d (ET)\n", cli);

            } else {
                // Edge-triggered: must drain the buffer completely
                char buf[64]{};
                while (true) {
                    ssize_t r = ::recv(fd, buf, sizeof(buf) - 1, 0);
                    if (r == 0) {
                        std::printf("  epoll: fd=%d EOF\n", fd);
                        ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        ::close(fd);
                        goto epoll_done;
                    }
                    if (r == -1) {
                        if (errno == EAGAIN) break; // drained
                        die("recv");
                    }
                    buf[r] = '\0';
                    std::printf("  epoll: fd=%d data='%s'\n", fd, buf);
                }
            }
        }
    }
epoll_done:
    ::close(epfd);
    ::close(srv);
    client_thread.join();
}

#elif defined(__APPLE__)

static void demo_kqueue()
{
    std::puts("\n── kqueue (macOS) demo ──────────────────────────────────");

    int srv = make_server(BASE_PORT + 2);
    int kq  = ::kqueue();
    if (kq == -1) die("kqueue");

    // Register: EV_ADD | EV_CLEAR = add + edge-triggered (clear on return)
    struct kevent change{};
    EV_SET(&change, static_cast<uintptr_t>(srv), EVFILT_READ,
           EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (::kevent(kq, &change, 1, nullptr, 0, nullptr) == -1)
        die("kevent register srv");

    std::thread client_thread([]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(30ms);
        int c = make_client(BASE_PORT + 2);
        ::send(c, "TICK1", 5, 0);
        ::send(c, "TICK2", 5, 0);
        std::this_thread::sleep_for(20ms);
        ::close(c);
    });

    struct kevent events[16];
    int           rounds = 0;

    while (rounds < 10) {
        struct timespec ts{ .tv_sec = 0, .tv_nsec = 500'000'000 };
        int n = ::kevent(kq, nullptr, 0, events, 16, &ts);
        if (n == 0) { std::puts("  kevent timeout"); break; }
        if (n < 0 && errno == EINTR) continue;
        ++rounds;

        for (int i = 0; i < n; ++i) {
            int fd = static_cast<int>(events[i].ident);

            if (fd == srv) {
                int cli = ::accept(srv, nullptr, nullptr);
                if (cli == -1) continue;
                set_nonblocking(cli);

                struct kevent cev{};
                EV_SET(&cev, static_cast<uintptr_t>(cli), EVFILT_READ,
                       EV_ADD | EV_CLEAR, 0, 0, nullptr);
                ::kevent(kq, &cev, 1, nullptr, 0, nullptr);
                std::printf("  kqueue: accepted fd=%d\n", cli);

            } else {
                char buf[64]{};
                while (true) {
                    ssize_t r = ::recv(fd, buf, sizeof(buf) - 1, 0);
                    if (r == 0) {
                        std::printf("  kqueue: fd=%d EOF\n", fd);
                        struct kevent del{};
                        EV_SET(&del, static_cast<uintptr_t>(fd),
                               EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                        ::kevent(kq, &del, 1, nullptr, 0, nullptr);
                        ::close(fd);
                        goto kqueue_done;
                    }
                    if (r == -1) {
                        if (errno == EAGAIN) break;
                        die("recv");
                    }
                    buf[r] = '\0';
                    std::printf("  kqueue: fd=%d data='%s'\n", fd, buf);
                }
            }
        }
    }
kqueue_done:
    ::close(kq);
    ::close(srv);
    client_thread.join();
}
#endif

int main()
{
    std::puts("── event-driven I/O demo ────────────────────────────────");

    demo_select();
    demo_poll();

#ifdef __linux__
    demo_epoll();
#elif defined(__APPLE__)
    demo_kqueue();
#else
    std::puts("epoll/kqueue not available on this platform");
#endif

    std::puts("\n── done ─────────────────────────────────────────────────");
}
