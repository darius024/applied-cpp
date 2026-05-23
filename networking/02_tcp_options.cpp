#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// ─────────────────────────────────────────────────────────────────────
// TCP socket options: SO_REUSEADDR, SO_REUSEPORT, TCP_NODELAY,
// SO_KEEPALIVE, SO_SNDBUF/SO_RCVBUF, SO_LINGER.
//
// setsockopt(fd, level, optname, &val, sizeof(val))
//   level: SOL_SOCKET for generic socket options
//          IPPROTO_TCP for TCP-specific options
//          IPPROTO_IP  for IP-level options
//
// TCP_NODELAY is the most impactful option for low-latency work:
// Nagle's algorithm batches small writes into one segment, which can
// delay a 40-byte order message by up to 200 ms while waiting for an
// ACK of in-flight data.  Disabling it sends every write immediately.
//
// SO_REUSEADDR + SO_REUSEPORT together allow multiple processes/threads
// to share a port and have the kernel load-balance connections.
//
// Compile: g++ -std=c++20 02_tcp_options.cpp -o tcp_opts && ./tcp_opts
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Helper: print a socket option value ──────────────────────────────
static void show_opt(int fd, int level, int opt, const char* name)
{
    int    val = 0;
    socklen_t len = sizeof(val);
    if (::getsockopt(fd, level, opt, &val, &len) == 0)
        std::printf("  %-30s = %d\n", name, val);
    else
        std::printf("  %-30s = (not supported)\n", name);
}

// ── PART 1: inspect defaults and then set options ────────────────────
static void demo_socket_options()
{
    std::puts("\n── socket option defaults ───────────────────────────────");

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");

    show_opt(fd, SOL_SOCKET,   SO_REUSEADDR,  "SO_REUSEADDR");
    show_opt(fd, SOL_SOCKET,   SO_REUSEPORT,  "SO_REUSEPORT");
    show_opt(fd, IPPROTO_TCP,  TCP_NODELAY,   "TCP_NODELAY");
    show_opt(fd, SOL_SOCKET,   SO_KEEPALIVE,  "SO_KEEPALIVE");
    show_opt(fd, SOL_SOCKET,   SO_SNDBUF,     "SO_SNDBUF");
    show_opt(fd, SOL_SOCKET,   SO_RCVBUF,     "SO_RCVBUF");

    std::puts("\n── after setting options ─────────────────────────────────");

    // SO_REUSEADDR — allow rebind during TIME_WAIT.
    // Set BEFORE bind(), otherwise the bind may fail on quick restart.
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // SO_REUSEPORT — multiple sockets on the same port.
    // Each thread/process creates its own listening socket; the kernel
    // hashes (src_ip, src_port) to pick a socket.  Eliminates a shared
    // accept() lock bottleneck under high connection rates.
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    // TCP_NODELAY — disable Nagle's algorithm.
    // Nagle waits until in-flight data is ACKed before sending a new
    // small segment.  For an order entry system sending 40-byte messages
    // this can add 40–200 ms latency.  Always disable for low-latency.
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // SO_KEEPALIVE — send keepalive probes to detect dead peers.
    // Default probe interval is 2 hours — tune with TCP_KEEPIDLE etc.
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

    // SO_SNDBUF / SO_RCVBUF — kernel socket buffer sizes.
    // For co-located systems the defaults (128 KiB–4 MiB) are fine.
    // For high-bandwidth, high-RTT paths, size to the Bandwidth-Delay
    // Product: BDP = bandwidth × RTT.
    // 10 Gbps × 1 ms RTT = 1.25 MiB minimum per direction.
    // Linux doubles the requested value; read back with getsockopt.
    int bufsz = 2 * 1024 * 1024; // ask for 2 MiB; kernel gives 4 MiB
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));

    show_opt(fd, SOL_SOCKET,   SO_REUSEADDR,  "SO_REUSEADDR");
    show_opt(fd, SOL_SOCKET,   SO_REUSEPORT,  "SO_REUSEPORT");
    show_opt(fd, IPPROTO_TCP,  TCP_NODELAY,   "TCP_NODELAY");
    show_opt(fd, SOL_SOCKET,   SO_KEEPALIVE,  "SO_KEEPALIVE");
    show_opt(fd, SOL_SOCKET,   SO_SNDBUF,     "SO_SNDBUF (actual)");
    show_opt(fd, SOL_SOCKET,   SO_RCVBUF,     "SO_RCVBUF (actual)");

    ::close(fd);
}

// ── PART 2: TCP_NODELAY latency impact ───────────────────────────────
// Run a loopback echo and measure round-trip time with/without Nagle.
// Nagle interacts with delayed ACK: without TCP_NODELAY the kernel may
// hold the write until a delayed ACK (40 ms on Linux) arrives, meaning
// even a tiny message can be delayed by 40 ms.
static constexpr int ECHO_PORT  = 19901;
static constexpr int NUM_PINGS  = 20;

static void echo_server(bool nodelay)
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (nodelay)
        setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(ECHO_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 4);

    int cli = ::accept(srv, nullptr, nullptr);
    if (nodelay)
        setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char buf[64];
    for (int i = 0; i < NUM_PINGS; ++i) {
        ssize_t n = ::recv(cli, buf, sizeof(buf), 0);
        if (n > 0) ::send(cli, buf, static_cast<std::size_t>(n), 0);
    }
    ::close(cli);
    ::close(srv);
}

static double ping_rtt_us(bool nodelay)
{
    using namespace std::chrono_literals;

    std::thread server_thread([nodelay]{ echo_server(nodelay); });
    std::this_thread::sleep_for(30ms);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (nodelay) setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(ECHO_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    char buf[64] = "PING";
    double total_us = 0.0;

    for (int i = 0; i < NUM_PINGS; ++i) {
        auto t0 = Clock::now();
        ::send(fd, buf, 4, 0);
        ::recv(fd, buf, sizeof(buf), 0);
        auto t1 = Clock::now();
        total_us += static_cast<double>(
            std::chrono::duration_cast<NS>(t1 - t0).count()) / 1000.0;
    }

    ::close(fd);
    server_thread.join();
    return total_us / NUM_PINGS;
}

// ── PART 3: SO_LINGER — RST vs FIN on close ──────────────────────────
// By default close() sends a FIN: a graceful TCP shutdown. The socket
// enters TIME_WAIT for 2×MSL (~120 s) to absorb any delayed packets.
//
// SO_LINGER with l_onoff=1, l_linger=0 sends a RST instead:
// immediate teardown, no TIME_WAIT. Useful for aborting connections
// without accumulating TIME_WAIT state on a busy server.
//
// Caution: RST may discard data still in the send buffer.
static void demo_linger()
{
    std::puts("\n── SO_LINGER (RST on close) ─────────────────────────────");

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");

    struct linger l{};
    l.l_onoff  = 1;  // enable linger
    l.l_linger = 0;  // timeout = 0 → send RST
    if (::setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == -1)
        die("setsockopt SO_LINGER");

    int linger_on  = 0;
    int linger_sec = 0;
    socklen_t slen = sizeof(l);
    getsockopt(fd, SOL_SOCKET, SO_LINGER, &l, &slen);
    linger_on  = l.l_onoff;
    linger_sec = l.l_linger;
    std::printf("  SO_LINGER: on=%d  timeout=%d s  → RST on close\n",
                linger_on, linger_sec);

    ::close(fd);
}

// ── PART 4: platform-specific keepalive tuning ───────────────────────
static void demo_keepalive()
{
    std::puts("\n── TCP keepalive tuning ─────────────────────────────────");

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

#ifdef __linux__
    int idle  = 10;  // seconds idle before first probe
    int intvl = 5;   // seconds between probes
    int cnt   = 3;   // number of probes before declaring dead
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
    std::printf("  TCP_KEEPIDLE=%d  TCP_KEEPINTVL=%d  TCP_KEEPCNT=%d\n",
                idle, intvl, cnt);
#elif defined(__APPLE__)
    int idle = 10; // macOS uses TCP_KEEPALIVE for idle timeout
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
    std::printf("  TCP_KEEPALIVE (idle)=%d s (macOS)\n", idle);
#else
    std::puts("  keepalive tuning not available on this platform");
#endif

    ::close(fd);
}

int main()
{
    std::puts("── TCP options demo ─────────────────────────────────────");

    demo_socket_options();
    demo_linger();
    demo_keepalive();

    // TCP_NODELAY latency: loopback RTT with and without Nagle.
    // On a loopback interface delayed-ACK is typically disabled, so the
    // difference may be small here.  On a real NIC the gap is larger.
    std::puts("\n── TCP_NODELAY latency (loopback ping-pong) ─────────────");
    double rtt_nagle    = ping_rtt_us(false);
    double rtt_nodelay  = ping_rtt_us(true);
    std::printf("  Nagle ON  (TCP_NODELAY=0): %.1f µs avg RTT\n", rtt_nagle);
    std::printf("  Nagle OFF (TCP_NODELAY=1): %.1f µs avg RTT\n", rtt_nodelay);
    std::printf("  Difference: %.1f µs\n", rtt_nagle - rtt_nodelay);

    std::puts("\n── done ─────────────────────────────────────────────────");
}
