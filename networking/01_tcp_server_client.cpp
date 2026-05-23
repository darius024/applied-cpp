#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

// ─────────────────────────────────────────────────────────────────────
// TCP server and client: socket, bind, listen, accept, connect,
// send, recv — the full lifecycle of a TCP connection.
//
// A TCP socket is a reliable, ordered, byte-stream channel.
// The server calls socket→bind→listen→accept (blocks until a client
// arrives). The client calls socket→connect. After the three-way
// handshake both sides can send and recv freely.
//
// This demo runs a simple echo server in a thread, then connects a
// client, sends market data lines, and receives them back.
//
// SO_REUSEADDR is set before bind so a quick restart after the demo
// doesn't fail with EADDRINUSE during TIME_WAIT.
//
// Compile: g++ -std=c++20 01_tcp_server_client.cpp -o tcp_demo && ./tcp_demo
// ─────────────────────────────────────────────────────────────────────

static constexpr int   PORT    = 19900;
static constexpr int   BACKLOG = 8;   // kernel's accept queue depth

// ── RAII helper: close fd on scope exit ──────────────────────────────
struct FdGuard {
    int fd;
    explicit FdGuard(int f) : fd(f) {}
    ~FdGuard() { if (fd >= 0) ::close(fd); }
    FdGuard(const FdGuard&)            = delete;
    FdGuard& operator=(const FdGuard&) = delete;
};

static void die(const char* msg)
{
    std::perror(msg);
    std::exit(1);
}

// ── Full-write: loop until all bytes are sent or error ───────────────
static void send_all(int fd, const void* buf, std::size_t len)
{
    const auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, 0);
        if (n <= 0) {
            if (n == -1 && errno == EINTR) continue;
            die("send");
        }
        p   += n;
        len -= static_cast<std::size_t>(n);
    }
}

// ── Full-read: loop until exactly `len` bytes are received ───────────
// Returns false when the peer closes the connection (EOF) before len
// bytes have arrived.
static bool recv_exactly(int fd, void* buf, std::size_t len)
{
    auto* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = ::recv(fd, p, len, 0);
        if (n == 0)  return false;          // EOF: peer closed
        if (n == -1) {
            if (errno == EINTR) continue;
            die("recv");
        }
        p   += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
}

// ── Echo server ───────────────────────────────────────────────────────
// Accepts one client, echoes every line back, then exits.
static void run_server()
{
    // ── 1. Create listening socket ────────────────────────────────────
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv == -1) die("socket");
    FdGuard srv_guard(srv);

    // SO_REUSEADDR: allow rebind during TIME_WAIT after quick restart
    int one = 1;
    if (::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
        die("setsockopt SO_REUSEADDR");

    // ── 2. Bind to port ───────────────────────────────────────────────
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // all local interfaces

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("bind");

    // ── 3. Listen: create the kernel accept queue ─────────────────────
    // BACKLOG = max pending (not yet accept()ed) connections.
    // Excess connections get a RST or are silently dropped depending on
    // the OS and tcp_abort_on_overflow sysctl.
    if (::listen(srv, BACKLOG) == -1)
        die("listen");

    std::puts("server: listening on port 19900");

    // ── 4. Accept one connection ──────────────────────────────────────
    sockaddr_in client_addr{};
    socklen_t   client_len = sizeof(client_addr);

    int cli = ::accept(srv, reinterpret_cast<sockaddr*>(&client_addr),
                       &client_len);
    if (cli == -1) die("accept");
    FdGuard cli_guard(cli);

    char peer_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip));
    std::printf("server: connection from %s:%d\n",
                peer_ip, ntohs(client_addr.sin_port));

    // ── 5. Echo loop: read a length-prefixed message, echo it back ────
    // Protocol: 4-byte big-endian length, then payload.
    // This avoids the "where does the message end?" problem inherent in
    // raw byte streams.
    for (;;) {
        uint32_t net_len = 0;
        if (!recv_exactly(cli, &net_len, 4)) break; // EOF
        uint32_t msg_len = ntohl(net_len);

        if (msg_len == 0 || msg_len > 4096) {
            std::puts("server: invalid message length, closing");
            break;
        }

        std::string msg(msg_len, '\0');
        if (!recv_exactly(cli, msg.data(), msg_len)) break;

        std::printf("server: echoing '%s'\n", msg.c_str());

        send_all(cli, &net_len, 4);          // echo length prefix
        send_all(cli, msg.data(), msg_len);  // echo payload
    }

    std::puts("server: client disconnected, shutting down");
    // srv_guard and cli_guard close fds automatically
}

// ── TCP client ────────────────────────────────────────────────────────
static void run_client()
{
    // Give the server a moment to bind and listen
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);

    // ── 1. Create socket ──────────────────────────────────────────────
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) die("socket");
    FdGuard guard(fd);

    // ── 2. Connect to server ──────────────────────────────────────────
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("connect");

    std::puts("client: connected");

    // ── 3. Send length-prefixed messages ─────────────────────────────
    const char* messages[] = {
        "BID 100.50 ASK 100.51 SZ 500",
        "TRADE 100.50 QTY 200",
        "BID 100.48 ASK 100.52 SZ 1000",
    };

    for (const char* text : messages) {
        uint32_t len     = static_cast<uint32_t>(std::strlen(text));
        uint32_t net_len = htonl(len);

        send_all(fd, &net_len, 4);
        send_all(fd, text, len);
        std::printf("client: sent '%s'\n", text);

        // Read echo back
        uint32_t rlen = 0;
        recv_exactly(fd, &rlen, 4);
        std::string reply(ntohl(rlen), '\0');
        recv_exactly(fd, reply.data(), reply.size());
        std::printf("client: got echo '%s'\n", reply.c_str());
    }

    // ── 4. Clean close: FIN handshake ────────────────────────────────
    // shutdown(SHUT_WR) sends a FIN to the server (we're done writing),
    // but we can still read any remaining data the server sends.
    // The server's recv loop will return 0 (EOF) after this.
    ::shutdown(fd, SHUT_WR);
    std::puts("client: sent FIN, done");
    // FdGuard closes fd (sends RST if there's unread data)
}

int main()
{
    std::puts("── TCP server/client demo ───────────────────────────────");

    // Run server in background thread, client in main thread
    std::thread server_thread(run_server);
    run_client();
    server_thread.join();

    std::puts("── done ─────────────────────────────────────────────────");
}
