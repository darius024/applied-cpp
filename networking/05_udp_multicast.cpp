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
// UDP multicast: IP_ADD_MEMBERSHIP, IP_MULTICAST_TTL, IP_MULTICAST_LOOP,
// source-specific multicast (SSM), and a simulated market data feed.
//
// Multicast sends one datagram that the network replicates to all
// registered receivers.  The sender sends once regardless of the number
// of receivers — crucial for market data distribution where one exchange
// feeds thousands of participants.
//
// IPv4 multicast uses Class D addresses: 224.0.0.0/4.
// Datacenter market data feeds typically use 239.x.x.x (org-local).
//
// Key socket options:
//   IP_ADD_MEMBERSHIP  — join a multicast group
//   IP_DROP_MEMBERSHIP — leave a group
//   IP_MULTICAST_TTL   — hop limit (1 = stay on local subnet)
//   IP_MULTICAST_LOOP  — whether sender receives its own packets
//   IP_MULTICAST_IF    — outgoing interface for multicast
//   IP_ADD_SOURCE_MEMBERSHIP — SSM: accept only from a specific sender
//
// Compile: g++ -std=c++20 05_udp_multicast.cpp -o multicast && ./multicast
// ─────────────────────────────────────────────────────────────────────

static constexpr const char* MCAST_GROUP = "239.1.2.3";
static constexpr int         MCAST_PORT  = 19907;
static constexpr int         NUM_PKTS    = 6;

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Market data packet (fits in one MTU) ──────────────────────────────
#pragma pack(push, 1)
struct FeedPacket {
    uint32_t seq;
    uint64_t exchange_ts_ns;
    char     feed_id[4];   // e.g. "XNYS" (NYSE)
    char     symbol[8];
    double   bid;
    double   ask;
    int32_t  bid_qty;
    int32_t  ask_qty;
    uint8_t  msg_type;     // 1=quote, 2=trade
    uint8_t  _pad[3];
};
#pragma pack(pop)

// ── PART 1: multicast receiver ────────────────────────────────────────
static void multicast_receiver(int id)
{
    // ── Create UDP socket ─────────────────────────────────────────────
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) die("socket");

    // SO_REUSEADDR allows multiple processes/threads to bind the same
    // multicast port simultaneously — essential for multicast receivers.
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // SO_REUSEPORT: on Linux, also set this to allow multiple sockets
    // on the same port to each receive a copy.
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    // ── Bind to the multicast port ────────────────────────────────────
    // Bind to INADDR_ANY (not the multicast address) so all interfaces
    // can deliver packets to this socket.
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MCAST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        die("bind");

    // ── Join the multicast group ──────────────────────────────────────
    // ip_mreq.imr_multiaddr: the group to join
    // ip_mreq.imr_interface: the local interface (INADDR_ANY = default)
    //
    // The kernel sends an IGMP membership report to the local router.
    // The router (or switch) then directs multicast traffic here.
    struct ip_mreq mreq{};
    inet_pton(AF_INET, MCAST_GROUP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq, sizeof(mreq)) == -1) {
        std::perror("IP_ADD_MEMBERSHIP");
        ::close(fd);
        return;
    }
    std::printf("receiver[%d]: joined %s:%d\n", id, MCAST_GROUP, MCAST_PORT);

    // ── Receive loop ──────────────────────────────────────────────────
    for (int i = 0; i < NUM_PKTS; ++i) {
        FeedPacket   pkt{};
        sockaddr_in  src{};
        socklen_t    src_len = sizeof(src);

        ssize_t n = ::recvfrom(fd, &pkt, sizeof(pkt), 0,
                               reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n <= 0) break;

        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        std::printf("receiver[%d]: seq=%u  %.8s  bid=%.2f ask=%.2f  "
                    "qty=%d/%d  from %s\n",
                    id, ntohl(pkt.seq), pkt.symbol,
                    pkt.bid, pkt.ask,
                    ntohl(static_cast<uint32_t>(pkt.bid_qty)),
                    ntohl(static_cast<uint32_t>(pkt.ask_qty)),
                    src_ip);
    }

    // ── Leave the group explicitly (also done automatically on close) ─
    ::setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    std::printf("receiver[%d]: left group, closing\n", id);
    ::close(fd);
}

// ── PART 2: multicast sender (feed publisher) ─────────────────────────
static void multicast_sender()
{
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(60ms); // let receivers join

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) die("socket");

    // ── TTL: how many router hops the packet survives ─────────────────
    // 1  = local subnet only (standard for datacenter feeds)
    // 32 = site-local
    // 64 = regional
    int ttl = 1;
    if (::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
                     &ttl, sizeof(ttl)) == -1)
        std::perror("IP_MULTICAST_TTL");

    // ── Loopback: does the sender also receive its own packets? ───────
    // Default is 1 (enabled). Disable for production to avoid the sender
    // processing its own feed.
    int loop = 1; // keep enabled so our receivers in this process get it
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    // ── Outgoing interface (optional) ────────────────────────────────
    // Bind the sender to a specific NIC. INADDR_ANY uses the routing table.
    struct in_addr iface{};
    iface.s_addr = htonl(INADDR_ANY);
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(MCAST_PORT);
    inet_pton(AF_INET, MCAST_GROUP, &dest.sin_addr);

    struct { const char* sym; double bid; double ask; int bq; int aq; }
    ticks[] = {
        { "AAPL    ", 182.50, 182.51, 500,  300  },
        { "MSFT    ", 415.20, 415.22, 1000, 800  },
        { "GOOG    ", 178.90, 178.95, 200,  200  },
        { "TSLA    ", 251.30, 251.35, 750,  600  },
        { "NVDA    ", 875.00, 875.10, 100,  150  },
        { "AMZN    ", 192.40, 192.45, 300,  350  },
    };

    std::puts("sender: publishing to multicast group 239.1.2.3");
    for (int i = 0; i < NUM_PKTS; ++i) {
        FeedPacket pkt{};
        pkt.seq              = htonl(static_cast<uint32_t>(i + 1));
        pkt.exchange_ts_ns   = static_cast<uint64_t>(
            std::chrono::duration_cast<NS>(
                Clock::now().time_since_epoch()).count());
        std::memcpy(pkt.feed_id, "XNYS", 4);
        std::memcpy(pkt.symbol, ticks[i].sym, 8);
        pkt.bid      = ticks[i].bid;
        pkt.ask      = ticks[i].ask;
        pkt.bid_qty  = htonl(static_cast<uint32_t>(ticks[i].bq));
        pkt.ask_qty  = htonl(static_cast<uint32_t>(ticks[i].aq));
        pkt.msg_type = 1; // quote

        ssize_t n = ::sendto(fd, &pkt, sizeof(pkt), 0,
                             reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        if (n == -1) std::perror("sendto multicast");
        else
            std::printf("sender:     seq=%u  %.8s  bid=%.2f  (%zd bytes)\n",
                        i + 1, pkt.symbol, pkt.bid, n);

        std::this_thread::sleep_for(10ms);
    }

    ::close(fd);
    std::puts("sender: done");
}

// ── PART 3: source-specific multicast (SSM) concept ──────────────────
// SSM restricts reception to a single sender address, preventing
// spoofed multicast injection.  Range 232.0.0.0/8 (IANA).
static void demo_ssm_join()
{
    std::puts("\n── SSM (source-specific multicast) join concept ─────────");

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) die("socket");

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MCAST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    // ip_mreq_source: accept only from a specific source
    struct ip_mreq_source mreqs{};
    inet_pton(AF_INET, "232.0.1.1",    &mreqs.imr_multiaddr);  // SSM group
    inet_pton(AF_INET, "10.0.0.1",     &mreqs.imr_sourceaddr); // exchange IP
    mreqs.imr_interface.s_addr = htonl(INADDR_ANY);

    int rc = ::setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                          &mreqs, sizeof(mreqs));
    if (rc == 0) {
        std::puts("  SSM join 232.0.1.1 from source 10.0.0.1: OK");
        ::setsockopt(fd, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP,
                     &mreqs, sizeof(mreqs));
    } else {
        std::perror("  IP_ADD_SOURCE_MEMBERSHIP");
    }

    ::close(fd);
}

int main()
{
    std::puts("── UDP multicast demo ───────────────────────────────────");

    // Two receivers subscribe to the same group; one sender publishes.
    // Both receivers get every packet (multicast replication).
    std::thread r1(multicast_receiver, 1);
    std::thread r2(multicast_receiver, 2);
    std::thread sender(multicast_sender);

    sender.join();
    r1.join();
    r2.join();

    demo_ssm_join();

    std::puts("\n── done ─────────────────────────────────────────────────");
}
