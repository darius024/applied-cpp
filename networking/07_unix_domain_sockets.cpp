#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────
// UNIX domain sockets (AF_UNIX): stream and datagram, filesystem and
// abstract namespace, credential passing (SCM_CREDENTIALS / LOCAL_PEERCRED).
//
// UNIX sockets communicate between processes on the same host.
// Compared to TCP loopback:
//   - No TCP overhead (no handshake, sequence numbers, ACKs, congestion)
//   - No IP checksum, no port namespace
//   - ~20–40% lower latency on co-located processes
//   - Supports ancillary data: SCM_RIGHTS (pass fds) and SCM_CREDENTIALS
//
// Two namespaces:
//   Filesystem path: "/tmp/myapp.sock" — must unlink() on close
//   Abstract (Linux): "\0myapp" — no filesystem entry, auto-removed
//
// Compile: g++ -std=c++20 07_unix_domain_sockets.cpp -o unix_socks && ./unix_socks
// ─────────────────────────────────────────────────────────────────────

static constexpr const char* SOCK_PATH = "/tmp/applied_cpp_demo.sock";

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Helpers ──────────────────────────────────────────────────────────
static void fill_addr(sockaddr_un& addr, const char* path)
{
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
}

// ── PART 1: SOCK_STREAM over filesystem path ──────────────────────────
// Identical API to TCP but over a local socket file.
static void stream_server()
{
    ::unlink(SOCK_PATH); // remove stale socket from previous run

    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv == -1) die("socket");

    sockaddr_un addr{};
    fill_addr(addr, SOCK_PATH);

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("bind");
    if (::listen(srv, 8) == -1)
        die("listen");

    std::puts("stream server: listening on " SOCK_PATH);

    int cli = ::accept(srv, nullptr, nullptr);
    if (cli == -1) die("accept");
    std::puts("stream server: accepted connection");

    // Echo loop
    char buf[256];
    for (;;) {
        ssize_t n = ::recv(cli, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        std::printf("stream server: received '%s'\n", buf);
        ::send(cli, buf, static_cast<std::size_t>(n), 0);
    }

    ::close(cli);
    ::close(srv);
    ::unlink(SOCK_PATH);
    std::puts("stream server: done");
}

static void stream_client()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(40ms);

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) die("socket");

    sockaddr_un addr{};
    fill_addr(addr, SOCK_PATH);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("connect");
    std::puts("stream client: connected");

    const char* messages[] = {
        "ORDER BUY AAPL 100",
        "ORDER SELL MSFT 50",
        "CANCEL 12345",
    };
    for (const char* msg : messages) {
        ::send(fd, msg, std::strlen(msg), 0);
        char reply[256]{};
        ssize_t n = ::recv(fd, reply, sizeof(reply) - 1, 0);
        if (n > 0) {
            reply[n] = '\0';
            std::printf("stream client: echo '%s'\n", reply);
        }
    }

    ::close(fd);
}

// ── PART 2: SOCK_DGRAM (connectionless UNIX socket) ──────────────────
// One socket bound as the "server", clients send datagrams to its path.
// No connect/accept needed.
static constexpr const char* DGRAM_SERVER_PATH = "/tmp/applied_cpp_dgram.sock";
static constexpr const char* DGRAM_CLIENT_PATH = "/tmp/applied_cpp_dgram_cli.sock";

static void dgram_server()
{
    ::unlink(DGRAM_SERVER_PATH);

    int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd == -1) die("socket dgram");

    sockaddr_un addr{};
    fill_addr(addr, DGRAM_SERVER_PATH);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("bind dgram server");

    std::puts("dgram server: bound to " DGRAM_SERVER_PATH);

    for (int i = 0; i < 3; ++i) {
        char       buf[256]{};
        sockaddr_un src{};
        socklen_t   src_len = sizeof(src);

        ssize_t n = ::recvfrom(fd, buf, sizeof(buf) - 1, 0,
                               reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n <= 0) break;
        buf[n] = '\0';
        std::printf("dgram server: '%s' from '%s'\n", buf, src.sun_path);
    }

    ::close(fd);
    ::unlink(DGRAM_SERVER_PATH);
}

static void dgram_client()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(40ms);

    int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd == -1) die("socket dgram client");

    // Bind the client socket so the server can see our path in recvfrom
    ::unlink(DGRAM_CLIENT_PATH);
    sockaddr_un self{};
    fill_addr(self, DGRAM_CLIENT_PATH);
    ::bind(fd, reinterpret_cast<sockaddr*>(&self), sizeof(self));

    sockaddr_un dest{};
    fill_addr(dest, DGRAM_SERVER_PATH);

    const char* msgs[] = { "TICK AAPL 182.50", "TICK MSFT 415.20", "DONE" };
    for (const char* m : msgs) {
        ::sendto(fd, m, std::strlen(m), 0,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        std::printf("dgram client: sent '%s'\n", m);
        std::this_thread::sleep_for(5ms);
    }

    ::close(fd);
    ::unlink(DGRAM_CLIENT_PATH);
}

// ── PART 3: abstract namespace (Linux only) ───────────────────────────
// The path starts with a null byte '\0'.  The socket lives in a kernel
// namespace — no file on disk, no unlink() needed.
#ifdef __linux__
static void demo_abstract_namespace()
{
    std::puts("\n── abstract namespace (Linux) ───────────────────────────");

    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv == -1) die("socket abstract");

    sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0'; // abstract namespace indicator
    const char* name = "applied_cpp_abstract";
    std::strncpy(addr.sun_path + 1, name, sizeof(addr.sun_path) - 2);
    // Length = family + 1 (null) + len(name)
    socklen_t addrlen = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + 1 + std::strlen(name));

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), addrlen) == -1)
        die("bind abstract");
    ::listen(srv, 4);
    std::puts("  abstract socket '\\0applied_cpp_abstract' bound (no file)");

    std::thread client_thread([&addr, addrlen]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(20ms);
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), addrlen) == -1)
            die("connect abstract");
        ::send(fd, "hello abstract", 14, 0);
        ::close(fd);
    });

    int cli = ::accept(srv, nullptr, nullptr);
    char buf[64]{};
    ssize_t n = ::recv(cli, buf, sizeof(buf) - 1, 0);
    buf[n > 0 ? n : 0] = '\0';
    std::printf("  abstract server received: '%s'\n", buf);
    ::close(cli);
    ::close(srv);
    // No unlink() — abstract socket disappears when last fd closes
    client_thread.join();
}
#endif

// ── PART 4: peer credentials (LOCAL_PEERCRED / SO_PEERCRED) ──────────
// The receiver can verify the PID, UID, GID of the connected process.
static void demo_peer_credentials()
{
    std::puts("\n── peer credentials ─────────────────────────────────────");

    static constexpr const char* CRED_PATH = "/tmp/applied_cpp_cred.sock";
    ::unlink(CRED_PATH);

    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    fill_addr(addr, CRED_PATH);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 4);

    std::thread client_thread([]{
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(20ms);
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        fill_addr(a, CRED_PATH);
        ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::send(fd, "auth", 4, 0);
        ::close(fd);
    });

    int cli = ::accept(srv, nullptr, nullptr);

    // Read the one byte so the connection is established
    char dummy[8]{};
    ::recv(cli, dummy, sizeof(dummy), 0);

#ifdef __linux__
    struct ucred cred{};
    socklen_t    cred_len = sizeof(cred);
    if (::getsockopt(cli, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0)
        std::printf("  peer: pid=%d  uid=%d  gid=%d\n",
                    cred.pid, cred.uid, cred.gid);
    else
        std::perror("  SO_PEERCRED");
#elif defined(__APPLE__)
    struct xucred cred{};
    socklen_t     cred_len = sizeof(cred);
    if (::getsockopt(cli, SOL_LOCAL, LOCAL_PEERCRED, &cred, &cred_len) == 0)
        std::printf("  peer: uid=%d  ngroups=%d\n",
                    cred.cr_uid, cred.cr_ngroups);
    else
        std::perror("  LOCAL_PEERCRED");
#else
    std::puts("  peer credentials not supported on this platform");
#endif

    ::close(cli);
    ::close(srv);
    ::unlink(CRED_PATH);
    client_thread.join();
}

int main()
{
    std::puts("── UNIX domain socket demo ──────────────────────────────");

    // SOCK_STREAM echo
    std::puts("\n── SOCK_STREAM ──────────────────────────────────────────");
    std::thread ss(stream_server);
    stream_client();
    ss.join();

    // SOCK_DGRAM
    std::puts("\n── SOCK_DGRAM ───────────────────────────────────────────");
    std::thread ds(dgram_server);
    dgram_client();
    ds.join();

#ifdef __linux__
    demo_abstract_namespace();
#endif

    demo_peer_credentials();

    std::puts("\n── done ─────────────────────────────────────────────────");
}
