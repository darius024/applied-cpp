# Networking in C++

Network programming in C++ is built on the POSIX Berkeley socket API — a thin
layer of syscalls that has remained essentially unchanged since BSD 4.2 in 1983.
Every language's networking library (Python's `socket`, Java's `java.net`, Boost.Asio,
libuv) is ultimately a wrapper around the same half-dozen syscalls:
`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `close`.

Understanding them at the syscall level gives you precise control over latency,
throughput, and resource usage — critical in quantitative finance where a market
data feed carries millions of UDP packets per second and tick-to-trade latency
is measured in microseconds.

---

## The Berkeley Socket API

### Socket as a file descriptor

A socket is just a file descriptor — a small non-negative integer the kernel uses to
identify an open resource. You read and write it with the same system calls as files.
The kernel maintains a socket buffer (send and receive) for each fd.

```
user space         kernel space
─────────────      ─────────────────────────────────────────
send(fd, buf)  →   copy buf → socket send buffer
                   TCP layer drains it onto the NIC
recv(fd, buf)  ←   copy from socket recv buffer → buf
                   NIC fills recv buffer as packets arrive
```

### Address families

| Family       | Constant    | Use                                    |
|--------------|-------------|----------------------------------------|
| IPv4         | `AF_INET`   | Classic internet addressing            |
| IPv6         | `AF_INET6`  | 128-bit internet addressing            |
| UNIX domain  | `AF_UNIX`   | Intra-host IPC via filesystem path     |

### Socket types

| Type           | Constant        | Transport |
|----------------|-----------------|-----------|
| Stream         | `SOCK_STREAM`   | TCP — byte-stream, reliable, ordered |
| Datagram       | `SOCK_DGRAM`    | UDP — message-oriented, unreliable   |
| Raw            | `SOCK_RAW`      | Direct IP — custom protocols, ping   |

`socket(AF_INET, SOCK_STREAM, 0)` creates a TCP/IPv4 socket.
`socket(AF_INET, SOCK_DGRAM,  0)` creates a UDP/IPv4 socket.

### Address structures

```cpp
// IPv4
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET
    in_port_t      sin_port;     // port in network byte order
    struct in_addr sin_addr;     // IPv4 address
};

// IPv6
struct sockaddr_in6 {
    sa_family_t     sin6_family;  // AF_INET6
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

// UNIX domain
struct sockaddr_un {
    sa_family_t sun_family;   // AF_UNIX
    char        sun_path[108];
};
```

Most syscalls accept `sockaddr*` — a generic pointer; cast from the concrete type.

### Byte ordering

Network protocols use **big-endian** (most significant byte first) byte order.
x86 hardware is little-endian. The conversion functions are:

```cpp
uint32_t htonl(uint32_t host_long);    // host → network (32-bit)
uint16_t htons(uint16_t host_short);   // host → network (16-bit)
uint32_t ntohl(uint32_t net_long);     // network → host (32-bit)
uint16_t ntohs(uint16_t net_short);    // network → host (16-bit)
```

Always apply these when writing to or reading from `sin_port` and `sin_addr`.

```cpp
addr.sin_port = htons(8080);
addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 0.0.0.0 — all interfaces
```

### Name resolution

`getaddrinfo` resolves hostnames and service names to socket-ready structs:

```cpp
struct addrinfo hints{};
hints.ai_family   = AF_UNSPEC;      // IPv4 or IPv6
hints.ai_socktype = SOCK_STREAM;    // TCP
struct addrinfo* res = nullptr;
getaddrinfo("exchange.co.uk", "9000", &hints, &res);
// use res->ai_addr, res->ai_addrlen, res->ai_family ...
freeaddrinfo(res);
```

---

## TCP fundamentals

### Three-way handshake

```
Client                      Server
  │                            │
  │──── SYN (seq=x) ──────────►│  Client sends SYN (synchronise)
  │◄─── SYN-ACK (seq=y,ack=x+1)│  Server acknowledges, picks its own seq
  │──── ACK (ack=y+1) ─────────►│  Client acknowledges
  │                            │
  │      connection open        │
```

The three-way handshake takes one full round-trip (RTT) before any data can
be sent. `TCP_FASTOPEN` (Linux) can embed data in the SYN packet for repeat
connections, saving that RTT.

### TCP state machine

Key states:

| State         | Meaning                                                  |
|---------------|----------------------------------------------------------|
| `LISTEN`      | Server waiting for incoming connections (`listen()`)    |
| `SYN_SENT`    | Client sent SYN, waiting for SYN-ACK                   |
| `ESTABLISHED` | Data transfer phase                                     |
| `FIN_WAIT_1`  | Active closer sent FIN, waiting for ACK                 |
| `FIN_WAIT_2`  | Got ACK of our FIN, waiting for peer's FIN              |
| `TIME_WAIT`   | Both FINs exchanged; waiting 2×MSL before reuse        |
| `CLOSE_WAIT`  | Got peer's FIN; application hasn't called `close()` yet |

### TIME_WAIT

After active close, the socket stays in `TIME_WAIT` for 2×MSL (Maximum Segment
Lifetime, typically 60 s on Linux → 120 s total). This ensures any delayed
duplicate packets are discarded before the port is reused.

**Problem**: rapid reconnection to the same address/port fails with `EADDRINUSE`.
**Fix**: `SO_REUSEADDR` lets a new socket bind even while the old one is in
`TIME_WAIT`, as long as no active socket holds the same tuple.

### Nagle's algorithm and TCP_NODELAY

Nagle's algorithm coalesces small writes into larger segments: it holds a small
write in the buffer until either:
- the buffer is full (one MSS, typically 1460 bytes), or
- an ACK arrives for in-flight data.

This improves throughput on bulk transfers but **adds up to 200 ms latency** on
interactive or low-latency workloads (e.g., sending a 40-byte order update and
waiting for a response).

**Fix**: `TCP_NODELAY` disables Nagle, sending each write immediately.

```cpp
int one = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
```

### Flow control and congestion control (brief)

- **Flow control**: the receiver advertises a window size; the sender cannot have
  more unacknowledged bytes in flight than the window. Prevents overwhelming
  a slow receiver.

- **Congestion control** (CUBIC, BBR, RENO): the sender probes network capacity
  with a slow-start phase, then backs off on loss or delay signals. Ensures
  fairness on shared links.

Both mechanisms mean TCP throughput can be well below line rate on high-latency
or lossy paths. For co-located systems (sub-millisecond RTT), they are rarely
the bottleneck.

---

## Socket options

Set with `setsockopt(fd, level, optname, &val, sizeof(val))`.
Read with `getsockopt(fd, level, optname, &val, &len)`.

### SO_REUSEADDR

```cpp
int one = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
```

Allows binding to a port that is in `TIME_WAIT`. **Always set this on a server
socket before `bind()`** — otherwise a restart after a crash will fail for up to
two minutes.

### SO_REUSEPORT

Allows multiple sockets on the same address:port. The kernel distributes incoming
connections/datagrams across all sockets. Enables a multi-threaded/multi-process
accept loop where each thread has its own socket, avoiding the
`accept()` thundering herd.

```cpp
int one = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
```

On Linux 3.9+. macOS supports it from 10.2.

### TCP_NODELAY

Disables Nagle's algorithm. Essential for any latency-sensitive application.

### SO_KEEPALIVE

Enables TCP keepalive probes when no data is sent for `tcp_keepalive_time`
(default 2 hours on Linux). Detects dead peers. On Linux, tune with:

```cpp
int idle  = 10;  // seconds idle before first probe
int intvl = 5;   // seconds between probes
int cnt   = 3;   // probes before declaring dead
setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
```

macOS uses `TCP_KEEPALIVE` instead of `TCP_KEEPIDLE`.

### SO_SNDBUF / SO_RCVBUF

Kernel socket buffers. Larger buffers allow more data in flight (important for
high-bandwidth, high-latency paths; see the BDP calculation below).

```cpp
int sz = 4 * 1024 * 1024; // 4 MiB
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
```

Linux doubles the requested size (kernel overhead). Check with `getsockopt`.

**Bandwidth-Delay Product (BDP)**:
$$\text{BDP} = \text{bandwidth} \times \text{RTT}$$
To fully utilise a 10 Gbps link with 1 ms RTT:
$$\text{BDP} = 10 \times 10^9 \div 8 \times 0.001 = 1.25 \text{ MiB}$$

The send and receive buffers must each be at least the BDP to keep the pipe full.

### SO_LINGER

Controls behaviour when `close()` is called with data in the send buffer.

```cpp
struct linger l { .l_onoff = 1, .l_linger = 0 };
setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
```

`l_onoff=1, l_linger=0`: sends an RST instead of FIN — immediate close, no
`TIME_WAIT`. Useful for server-side cleanup of aborted connections.

---

## UDP

### Datagram model

UDP is connectionless and message-oriented. Each `sendto()` call produces exactly
one datagram. Each `recvfrom()` call receives exactly one datagram. There is no
byte-stream abstraction — if you `sendto` 1000 bytes and the receiver calls
`recvfrom` with a 500-byte buffer, the remaining 500 bytes are silently discarded
(on Linux; on some systems you can use `MSG_PEEK` + `MSG_TRUNC` to query size first).

UDP guarantees:
- Nothing. Packets can be lost, duplicated, reordered, or corrupted.
- The kernel does verify the UDP checksum. A corrupt packet is dropped.
- A datagram is delivered as a whole or not at all — no partial delivery.

For market data, this is acceptable: the feed sender retransmits via a TCP
recovery channel; receivers use sequence numbers to detect gaps and request
retransmission only for missed messages.

### MTU and fragmentation

The standard Ethernet MTU is 1500 bytes. The IP header is 20 bytes, UDP header 8
bytes, leaving **1472 bytes for the UDP payload**.

Sending a larger datagram causes IP fragmentation: the kernel splits it into
fragments that are reassembled at the destination. One lost fragment causes the
whole datagram to be lost. **Avoid fragmentation** in performance-sensitive code:
keep UDP payloads ≤ 1472 bytes, or negotiate with `IP_PMTUDISC_DO` to discover
the path MTU.

Jumbo frames (9000 byte MTU) are common in datacenter networks; on those links
you can use payloads up to ~8972 bytes.

### Connected UDP

Calling `connect()` on a UDP socket stores the peer address in the kernel.
Subsequent calls can use `send()`/`recv()` instead of `sendto()`/`recvfrom()`.
This avoids the address lookup overhead per call and restricts incoming packets
to the connected peer (others are silently dropped).

---

## Multicast

### Class D addresses

IPv4 multicast uses `224.0.0.0/4` (224.0.0.0 – 239.255.255.255).

| Range               | Use                                         |
|---------------------|---------------------------------------------|
| `224.0.0.0/24`      | Link-local (TTL=1, routers do not forward)  |
| `224.0.1.0–238.x.x.x` | Globally scoped (IANA assigned)          |
| `239.0.0.0/8`       | Organisation-local (private, like RFC 1918) |

Financial market data feeds typically use `239.x.x.x` within a datacenter.

### IGMP and group management

The Internet Group Management Protocol (IGMP) is how a host tells a local router
it wants multicast traffic for a group. The kernel handles IGMP automatically when
you join a group with `IP_ADD_MEMBERSHIP`.

### Joining a group

```cpp
struct ip_mreq mreq{};
inet_pton(AF_INET, "239.1.2.3",  &mreq.imr_multiaddr);
inet_pton(AF_INET, "0.0.0.0",   &mreq.imr_interface); // INADDR_ANY = default interface
setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
```

Leave with `IP_DROP_MEMBERSHIP` (same struct). The kernel also leaves
automatically when `close()` is called.

### Sending multicast

```cpp
// Set TTL — how many router hops before the packet is discarded
int ttl = 1; // stay on local subnet
setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

// Disable loopback — sender receives its own packets by default
int loop = 0;
setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

// Bind sender to a specific outgoing interface
struct in_addr iface{};
inet_pton(AF_INET, "192.168.1.10", &iface);
setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
```

### Source-Specific Multicast (SSM)

SSM (`232.0.0.0/8`) ties group membership to a specific source address.
Receivers only accept datagrams from that source, preventing spoofing:

```cpp
struct ip_mreq_source mreqs{};
inet_pton(AF_INET, "232.0.1.1",        &mreqs.imr_multiaddr);
inet_pton(AF_INET, "192.168.1.10",     &mreqs.imr_sourceaddr); // sender IP
mreqs.imr_interface.s_addr = INADDR_ANY;
setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreqs, sizeof(mreqs));
```

---

## I/O models

Five POSIX I/O models, ordered by complexity and scalability:

### 1. Blocking I/O

`recv()` blocks the calling thread until data arrives. Simple and correct for
one connection per thread. Fails badly at thousands of concurrent connections
because each thread consumes kernel stack (~8 KiB) and scheduling overhead.

### 2. Non-blocking I/O

Set `O_NONBLOCK` with `fcntl()`. `recv()` returns immediately with `EAGAIN`
(same value as `EWOULDBLOCK`) when no data is available.

```cpp
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

**EINPROGRESS**: when `connect()` is called on a non-blocking socket, it returns
`-1` with `errno == EINPROGRESS`. The connection is completing asynchronously.
Use `poll`/`select`/`epoll` on the socket for writability to detect completion;
then `getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)` to check success.

### 3. select() / poll()

Watch multiple file descriptors for readability, writability, or error.

```cpp
// poll — preferred over select (no fd set size limit)
struct pollfd fds[2];
fds[0] = { server_fd, POLLIN, 0 };
fds[1] = { client_fd, POLLIN | POLLOUT, 0 };
int n = poll(fds, 2, 1000 /*ms timeout*/);
if (fds[0].revents & POLLIN) { /* accept */ }
if (fds[1].revents & POLLIN) { /* recv  */ }
```

`select()` is limited to `FD_SETSIZE` (1024) file descriptors on many systems.
`poll()` has no such limit. Both O(n) scan all registered fds on each call.

### 4. epoll (Linux) / kqueue (macOS)

Register interest once; get only the ready fds back — O(1) per event.

```cpp
// Linux epoll
int epfd = epoll_create1(EPOLL_CLOEXEC);
struct epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.fd = fd };
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1 /*block*/);
for (int i = 0; i < n; i++) {
    // events[i].data.fd is ready
}
```

**Level-triggered (LT)** (default): `epoll_wait` keeps returning the fd as ready
until the buffer is drained. Simpler but can loop if you don't drain.

**Edge-triggered (ET)** (`EPOLLET`): fires once when state changes from
not-ready → ready. Faster (fewer wakeups) but you **must** drain the socket
completely (loop until `EAGAIN`) or you'll miss data.

```cpp
// macOS kqueue equivalent
int kq = kqueue();
struct kevent change{ .ident = (uintptr_t)fd, .filter = EVFILT_READ,
                      .flags = EV_ADD | EV_CLEAR };
struct kevent events[64];
kevent(kq, &change, 1, nullptr, 0, nullptr);
int n = kevent(kq, nullptr, 0, events, 64, nullptr /*block*/);
```

### 5. io_uring (Linux 5.1+)

Eliminates the syscall overhead entirely for bulk I/O. Two ring buffers (submission
and completion) shared between user and kernel:

```
User space                    Kernel
──────────────────────────    ──────────────────────────────
┌──────────────────────┐      ┌──────────────────────────┐
│  Submission Ring (SQ)│─────►│  Kernel processes SQEs   │
│  sqe: op, fd, buf, … │      │  accept, recv, send, …   │
└──────────────────────┘      └──────────────────────────┘
┌──────────────────────┐      ┌──────────────────────────┐
│  Completion Ring (CQ)│◄─────│  Results posted as CQEs  │
│  cqe: user_data, res │      │  res = return value      │
└──────────────────────┘      └──────────────────────────┘
```

Key benefits:
- `IORING_SETUP_SQPOLL`: kernel polls the SQ; user never calls `io_uring_enter`.
  Zero syscalls for submission when the kernel thread is running.
- `IORING_FEAT_FAST_POLL`: uses an optimised poll path, removing a layer of
  indirect dispatch.
- Fixed buffers (`io_uring_register_buffers`): avoid per-operation address
  translation (DMA pinning).

---

## Zero-copy I/O

Normally: `disk → kernel buffer → user buffer → kernel socket buffer → NIC`.
Two copies through user space.

### sendfile

```cpp
// Linux
#include <sys/sendfile.h>
sendfile(socket_fd, file_fd, &offset, count);

// macOS
#include <sys/socket.h>
sendfile(file_fd, socket_fd, offset, &len, nullptr, 0);
```

The kernel copies directly from the page cache to the socket buffer. No user-space
copy. Ideal for HTTP file serving or replay of stored market data.

### splice (Linux)

Moves data between two kernel buffers (pipe ↔ fd) without copying to user space:

```cpp
splice(src_fd, nullptr, pipe_fds[1], nullptr, count, SPLICE_F_MOVE);
splice(pipe_fds[0], nullptr, dst_fd, nullptr, count, SPLICE_F_MOVE);
```

### MSG_ZEROCOPY (Linux 4.14+)

For sends: tells the kernel to use the user buffer directly (DMA from user pages):

```cpp
int one = 1;
setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
send(fd, buf, len, MSG_ZEROCOPY);
// Must drain the error queue to know when the buffer is safe to reuse:
struct msghdr msg{}; msg.msg_control = cmsg_buf; msg.msg_controllen = sizeof cmsg_buf;
recvmsg(fd, &msg, MSG_ERRQUEUE);
```

Overhead from the completion notification makes `MSG_ZEROCOPY` worthwhile only for
large sends (typically > 10 KiB).

---

## UNIX domain sockets

Communicate between processes on the same host via a filesystem path (or abstract
name). Compared to TCP loopback:
- **No TCP overhead**: no handshake, no sequence numbers, no ACKs.
- **No port namespace conflict**.
- **Credential passing**: `SCM_CREDENTIALS` lets a receiver verify the PID/UID/GID
  of the sender.
- **File descriptor passing**: `SCM_RIGHTS` sends open fds across process boundaries.

### Abstract namespace (Linux only)

A path starting with `\0` lives in an anonymous namespace, not the filesystem.
No `unlink()` needed on close; automatically removed when the last fd closes.

```cpp
struct sockaddr_un addr{};
addr.sun_family = AF_UNIX;
addr.sun_path[0] = '\0';
strncpy(addr.sun_path + 1, "myservice", sizeof(addr.sun_path) - 2);
size_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen("myservice");
bind(fd, (struct sockaddr*)&addr, len);
```

---

## Performance tuning

### Interrupt coalescing and busy polling

NIC interrupts are expensive. Linux `ethtool -C eth0 rx-usecs 50` coalesces
interrupts: the NIC waits 50 µs before raising an IRQ, batching several packets
into one interrupt. Improves throughput but adds latency.

**SO_BUSY_POLL** (Linux): the kernel polls the NIC ring directly in the socket
`recv` syscall, bypassing the IRQ/softirq path:

```cpp
int us = 50; // microseconds to spin-poll
setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &us, sizeof(us));
```

Reduces median latency at the cost of CPU core spinning.

### TCP_CORK

Accumulate data until `TCP_CORK` is cleared or MSS is reached, then flush as one
segment. Useful for constructing a response in several `write()` calls without
intermediate Nagle delays:

```cpp
int one = 1;
setsockopt(fd, IPPROTO_TCP, TCP_CORK, &one, sizeof(one));
write(fd, header, hlen);
write(fd, body,   blen);
int zero = 0;
setsockopt(fd, IPPROTO_TCP, TCP_CORK, &zero, sizeof(zero)); // flush
```

### SO_REUSEPORT multi-queue accept

Each thread creates its own listening socket on the same port. The kernel
load-balances incoming connections. Eliminates the `accept()` lock contention
bottleneck seen with a shared listening socket.

### Kernel bypass (DPDK / RDMA)

For the lowest latency (< 1 µs), kernel bypass moves the NIC driver entirely into
user space (DPDK) or uses RDMA (Remote Direct Memory Access) to read/write peer
memory without involving either CPU. Beyond the scope of this module.

---

## Market data patterns

### UDP multicast feed

The dominant pattern for market data distribution in a co-located datacenter:

```
Exchange matching engine
  │
  └─ Multicast publisher (UDP, 239.x.x.x)
       ├─ Trading engine A  (IP_ADD_MEMBERSHIP)
       ├─ Trading engine B
       └─ Risk monitor
```

Each packet carries a sequence number. Receivers detect gaps and request
retransmission via a TCP **recovery channel** (separate connection to an
exchange retransmit server). Gap detection and recovery must complete within
the **arbitrage window** before the mispricing opportunity disappears.

Key parameters:
- Packet rate: 100 K – 10 M packets/sec on a busy equity feed.
- Payload: 40 – 200 bytes per packet.
- UDP socket receive buffer: size to absorb bursts without packet drop.

### TCP vs UDP for order entry

Order entry (client → exchange) almost always uses TCP:
- Reliability is required — a lost order acknowledgement is catastrophic.
- Latency is controlled with `TCP_NODELAY` and buffer tuning.
- The exchange uses FIX or a proprietary binary protocol over TCP.

Some ultra-low-latency venues offer UDP order entry with application-level
ACKs and replay, but this is the exception.

### Sequenced multicast with recovery

A robust receiver implementation:

```
recv UDP packet
├── sequence == expected  →  process, expected++
├── sequence > expected   →  gap detected
│     └── request_retransmit(expected, sequence-1) via TCP recovery channel
│         mark messages as buffered, apply when gap filled
└── sequence < expected   →  duplicate, discard
```
