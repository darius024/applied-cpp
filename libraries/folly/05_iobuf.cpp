#include <folly/io/IOBuf.h>
#include <folly/io/Cursor.h>
#include <iostream>
#include <cstring>

// compile: g++ -std=c++17 05_iobuf.cpp -o 05_iobuf \
//          -lfolly -lglog -lgflags -lfmt -ldouble-conversion \
//          -lboost_system -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::IOBuf: reference-counted, chainable byte buffer.
//
// Layout of a single IOBuf (one contiguous allocation):
//
//   [ headroom | data_     | tailroom ]
//              ^           ^
//              data()      tail()
//
//   headroom: space to prepend headers (e.g. TCP/IP framing) without
//             copying the payload.
//   tailroom: space to append more payload bytes.
//
// Chaining: multiple IOBufs form a circular linked list.
//   appendChain() links them; the chain is iterated as a scatter/gather
//   view for sendmsg / writev syscalls — no copy needed.
//
// Cursor: read/write helpers that walk a chain transparently.
//   io::Cursor  — read-only; has read<T>(), pull(), skip(), etc.
//   io::RWPrivateCursor — writable; has write<T>(), push(), etc.
//
// HPC relevance: eliminates memcpy when building protocol messages
// or chaining buffers from different I/O layers. Used by Proxygen
// (Meta's HTTP/2 server) and fbthrift for all serialisation.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Create and write into a buffer ───────────────────────────────
    auto buf = folly::IOBuf::create(256); // 256 bytes capacity
    std::cout << "capacity: " << buf->capacity()  << "\n"; // 256
    std::cout << "length:   " << buf->length()    << "\n"; // 0

    // append() extends the data region into tailroom
    const char* msg = "Hello, IOBuf!";
    std::memcpy(buf->writableTail(), msg, std::strlen(msg));
    buf->append(std::strlen(msg));
    std::cout << "length after append: " << buf->length() << "\n"; // 13

    // ── Prepend a header via headroom ─────────────────────────────────
    // Reserve headroom up front; the buffer starts at tail of headroom.
    auto frame = folly::IOBuf::create(512);
    frame->reserve(4 /*headroom*/, 0);          // add 4 bytes of headroom
    const char* payload = "payload";
    std::memcpy(frame->writableTail(), payload, std::strlen(payload));
    frame->append(std::strlen(payload));

    frame->prepend(4);                           // move data() back 4 bytes
    uint32_t magic = 0xDEADBEEF;
    std::memcpy(frame->writableData(), &magic, 4);
    std::cout << "frame total: " << frame->length() << " bytes\n"; // 11

    // ── Chain two buffers ─────────────────────────────────────────────
    auto head = folly::IOBuf::create(64);
    const char* part1 = "Hello, ";
    std::memcpy(head->writableTail(), part1, std::strlen(part1));
    head->append(std::strlen(part1));

    auto tail_buf = folly::IOBuf::create(64);
    const char* part2 = "world!";
    std::memcpy(tail_buf->writableTail(), part2, std::strlen(part2));
    tail_buf->append(std::strlen(part2));

    head->appendChain(std::move(tail_buf));  // link into circular list

    std::cout << "chain length: " << head->computeChainDataLength() << "\n"; // 13

    // ── Read chain with Cursor ────────────────────────────────────────
    folly::io::Cursor cursor(head.get());
    std::string result;
    result.resize(head->computeChainDataLength());
    cursor.pull(result.data(), result.size());
    std::cout << "cursor read: " << result << "\n"; // Hello, world!

    // ── Clone (copy-on-write) ─────────────────────────────────────────
    auto clone = head->clone();                 // shares underlying buffer (refcount++)
    clone->unshare();                           // breaks sharing; now has own copy
    std::cout << "clone is shared: " << std::boolalpha
              << clone->isShared() << "\n";     // false after unshare
}
