#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

// ─────────────────────────────────────────────────────────────────────
// UDP basics: sendto, recvfrom, connectionless and connected modes.
//
// UDP is a datagram protocol: each sendto() produces exactly one
// datagram, and each recvfrom() consumes exactly one datagram.  There
// is no connection, no ordering guarantee, and no retransmission.
//
// Each datagram is either delivered intact or dropped entirely —
// the kernel verifies the UDP checksum and silently discards corrupt
// packets.  Partial delivery does not occur.
//
// MTU consideration: standard Ethernet MTU is 1500 bytes.
//   IP header:  20 bytes
//   UDP header:  8 bytes
//   Max payload: 1472 bytes without fragmentation
// Sending more causes IP fragmentation; one lost fragment loses the
// whole datagram.  Keep payloads ≤ 1472 bytes on standard networks.
//
// Compile: g++ -std=c++20 04_udp_basics.cpp -o udp_basics && ./udp_basics
// ─────────────────────────────────────────────────────────────────────

static constexpr int PORT       = 19905;
static constexpr int NUM_MSGS   = 5;

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Simulated market data packet ─────────────────────────────────────
// A compact binary layout fitting well within one MTU.
#pragma pack(push, 1)
struct MarketPacket {
    uint32_t seq;          // sequence number (network byte order)
    uint64_t timestamp_ns; // sender timestamp
    double   bid;
    double   ask;
    int32_t  bid_size;
    int32_t  ask_size;
    char     symbol[8];    // null-padded, e.g. "AAPL\0\0\0\0"
};
#pragma pack(pop)

static_assert(sizeof(MarketPacket) <= 1472,
              "MarketPacket exceeds UDP payload limit");

// ── PART 1: basic UDP sender/receiver ────────────────────────────────
static void udp_receiver()
{
    // ── Create UDP socket ─────────────────────────────────────────────
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) die("socket");

    // SO_REUSEADDR for quick restart
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // ── Bind to port ──────────────────────────────────────────────────
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("bind");

    std::puts("receiver: bound to port 19905");

    // ── Receive loop ──────────────────────────────────────────────────
    for (int i = 0; i < NUM_MSGS; ++i) {
        MarketPacket pkt{};
        sockaddr_in  sender{};
        socklen_t    sender_len = sizeof(sender);

        // recvfrom: fills sender address; use recv() if you don't need it.
        ssize_t n = ::recvfrom(fd, &pkt, sizeof(pkt), 0,
                               reinterpret_cast<sockaddr*>(&sender),
                               &sender_len);
        if (n == -1) die("recvfrom");
        if (n != static_cast<ssize_t>(sizeof(pkt))) {
            std::printf("receiver: unexpected packet size %zd (expected %zu)\n",
                        n, sizeof(pkt));
            continue;
        }

        // Convert from network byte order
        uint32_t seq = ntohl(pkt.seq);
        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));

        std::printf("receiver: seq=%u  %s  bid=%.2f ask=%.2f  "
                    "from %s:%d\n",
                    seq, pkt.symbol, pkt.bid, pkt.ask,
                    sender_ip, ntohs(sender.sin_port));
    }

    std::puts("receiver: done");
    ::close(fd);
}

static void udp_sender()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(40ms); // let receiver bind

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) die("socket");

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    struct { const char* symbol; double bid; double ask; int bsz; int asz; }
    quotes[] = {
        { "AAPL", 182.50, 182.51, 500,  300  },
        { "MSFT", 415.20, 415.22, 1000, 800  },
        { "GOOG", 178.90, 178.95, 200,  200  },
        { "TSLA", 251.30, 251.35, 750,  600  },
        { "NVDA", 875.00, 875.10, 100,  150  },
    };

    for (int i = 0; i < NUM_MSGS; ++i) {
        MarketPacket pkt{};
        pkt.seq          = htonl(static_cast<uint32_t>(i + 1));
        pkt.timestamp_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<NS>(
                Clock::now().time_since_epoch()).count());
        pkt.bid      = quotes[i].bid;
        pkt.ask      = quotes[i].ask;
        pkt.bid_size = quotes[i].bsz;
        pkt.ask_size = quotes[i].asz;
        std::strncpy(pkt.symbol, quotes[i].symbol, sizeof(pkt.symbol));

        // sendto: each call is one UDP datagram
        ssize_t n = ::sendto(fd, &pkt, sizeof(pkt), 0,
                             reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        if (n == -1) die("sendto");

        std::printf("sender:   seq=%u  %s  bid=%.2f ask=%.2f  "
                    "(%zd bytes)\n",
                    i + 1, pkt.symbol, pkt.bid, pkt.ask, n);

        std::this_thread::sleep_for(5ms);
    }

    ::close(fd);
}

// ── PART 2: connected UDP ─────────────────────────────────────────────
// Calling connect() on a UDP socket stores the peer address in the
// kernel.  Subsequent calls use send()/recv() instead of sendto()/
// recvfrom().  Datagrams from other sources are silently dropped.
//
// Benefits:
//   - Slightly lower per-call overhead (no address lookup)
//   - ICMP errors (e.g. port unreachable) are delivered as errno
//   - Can use send()/recv() with no address argument
static void demo_connected_udp()
{
    std::puts("\n── connected UDP ─────────────────────────────────────────");

    // Receiver side (listen on a different port)
    constexpr int CPORT = 19906;

    int recv_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    {
        int one = 1;
        ::setsockopt(recv_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family      = AF_INET;
        a.sin_port        = htons(CPORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(recv_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }

    int send_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    {
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port   = htons(CPORT);
        inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);

        // connect() on UDP just stores the peer address — no handshake
        if (::connect(send_fd,
                      reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) == -1)
            die("connect UDP");
        std::puts("  sender connected (no handshake, just address stored)");
    }

    // Use send() / recv() — no address arguments
    const char* msg = "ORDER BUY AAPL 100 @ 182.50";
    ssize_t n = ::send(send_fd, msg, std::strlen(msg), 0);
    std::printf("  sent %zd bytes via send() (connected UDP)\n", n);

    char buf[256]{};
    // Non-blocking check with a timeout using poll
    struct pollfd pfd{ .fd = recv_fd, .events = POLLIN, .revents = 0 };
    if (::poll(&pfd, 1, 500) > 0) {
        ssize_t r = ::recv(recv_fd, buf, sizeof(buf) - 1, 0);
        std::printf("  recv: '%s'\n", buf);
        (void)r;
    }

    ::close(send_fd);
    ::close(recv_fd);
}

// ── PART 3: detect dropped datagrams ─────────────────────────────────
// UDP has no delivery guarantee.  Production code tracks sequence numbers
// and counts gaps.
static void demo_gap_detection()
{
    std::puts("\n── sequence number gap detection ────────────────────────");

    // Simulate receiving datagrams with a gap (seq 3 missing)
    uint32_t received[] = { 1, 2, 4, 5 };
    uint32_t expected   = 1;
    int      gaps       = 0;

    for (uint32_t seq : received) {
        if (seq > expected) {
            std::printf("  GAP detected: expected seq %u, got %u "
                        "(%u packet(s) missing)\n",
                        expected, seq, seq - expected);
            gaps += static_cast<int>(seq - expected);
            expected = seq + 1;
        } else if (seq == expected) {
            std::printf("  OK  seq=%u\n", seq);
            ++expected;
        } else {
            std::printf("  DUPLICATE seq=%u (expected %u)\n", seq, expected);
        }
    }

    std::printf("  total gaps: %d packet(s)\n", gaps);
    std::puts("  → request retransmit from TCP recovery channel");
}

int main()
{
    std::puts("── UDP basics demo ──────────────────────────────────────");

    std::thread receiver_thread(udp_receiver);
    udp_sender();
    receiver_thread.join();

    demo_connected_udp();
    demo_gap_detection();

    std::puts("\n── done ─────────────────────────────────────────────────");
}
