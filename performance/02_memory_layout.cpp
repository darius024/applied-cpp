#include <cstdint>
#include <cstdio>
#include <new>
#include <type_traits>

// ─────────────────────────────────────────────────────────────────────
// Memory layout: struct padding, member reordering, alignas, cache-line
// alignment, and static_assert layout locks.
//
// The compiler inserts padding bytes between struct members to satisfy
// alignment requirements:
//   - char   (1 byte) — no alignment needed
//   - short  (2 bytes) — must start at an even address
//   - int    (4 bytes) — address must be a multiple of 4
//   - double (8 bytes) — address must be a multiple of 8
//   - pointer          — multiple of 8 on 64-bit
//
// The struct's total size is rounded up to a multiple of its largest
// member's alignment (the "trailing padding" rule).
//
// Why it matters:
//   1) A padded struct wastes memory — more data per cache line means
//      better hit rates, especially in arrays.
//   2) Misaligned SIMD loads are slower (or illegal).
//   3) Unintended layout changes break binary protocols / shared memory.
//
// alignas(N): forces the start address to be a multiple of N.
//   alignas(64) = cache-line alignment — prevents false sharing between
//   separate hot counters or per-thread state.
//   alignas(32) = 256-bit AVX alignment — enables aligned SIMD loads.
//
// Compile:  g++ -std=c++20 -O2 02_memory_layout.cpp -o layout && ./layout
// ─────────────────────────────────────────────────────────────────────

// ── Helper: print struct size and alignment ───────────────────────────
#define SHOW(T) \
    std::printf("  %-26s  sizeof=%2zu  alignof=%zu\n", \
                #T, sizeof(T), alignof(T))

// ── PART 1: padding created by member ordering ─────────────────────────
// Members are laid out in declaration order.  The compiler inserts
// padding before each member so it is naturally aligned.

struct BadOrder {
    char   a;       // 1 byte  → 1 byte used
    // 7 bytes padding
    double b;       // 8 bytes aligned to 8
    char   c;       // 1 byte
    // 7 bytes trailing padding (so array stride is multiple of 8)
    int    d;       // wait — order matters; let's trace exactly:
};
// Actual layout of BadOrder: a(1)+pad(3)+d... let me spell it out:

struct PaddedBad {
    char   a;    // offset 0,  size 1
    // 3 bytes padding
    int    b;    // offset 4,  size 4
    char   c;    // offset 8,  size 1
    // 7 bytes padding
    double d;    // offset 16, size 8
};                // total: 24 bytes  (7 wasted)

struct PaddedGood {
    double d;    // offset 0,  size 8
    int    b;    // offset 8,  size 4
    char   a;    // offset 12, size 1
    char   c;    // offset 13, size 1
    // 2 bytes trailing padding
};                // total: 16 bytes  (2 wasted)

static_assert(sizeof(PaddedBad)  == 24, "layout changed");
static_assert(sizeof(PaddedGood) == 16, "layout changed");

// ── PART 2: real-world struct for a market order ──────────────────────
// Before: naively ordered. After: sorted largest-to-smallest member.

struct OrderBad {
    bool      is_buy;    // 1 byte
    // 7 bytes padding
    double    price;     // 8 bytes
    bool      ioc;       // 1 byte
    // 3 bytes padding
    int       qty;       // 4 bytes
    char      symbol[8]; // 8 bytes
    // no trailing pad (already multiple of 8)
};                       // total: 32 bytes

struct OrderGood {
    double    price;     // offset 0,  8 bytes
    char      symbol[8]; // offset 8,  8 bytes
    int       qty;       // offset 16, 4 bytes
    bool      is_buy;    // offset 20, 1 byte
    bool      ioc;       // offset 21, 1 byte
    // 2 bytes trailing padding
};                       // total: 24 bytes

static_assert(sizeof(OrderBad)  == 32, "layout changed");
static_assert(sizeof(OrderGood) == 24, "layout changed");

// ── PART 3: alignas — cache-line and SIMD alignment ───────────────────

// Per-core accumulator — align to 64 bytes to prevent false sharing
// between cores operating on adjacent array entries.
struct alignas(64) CoreAccumulator {
    double pnl{0.0};
    long   trade_count{0};
    // (padding to 64 bytes implicit — alignas forces struct size to 64)
};
static_assert(alignof(CoreAccumulator) == 64, "alignment wrong");
static_assert(sizeof(CoreAccumulator)  == 64, "size wrong");

// Buffer aligned for 256-bit AVX loads (32-byte alignment).
struct alignas(32) PriceBuffer {
    double prices[4]; // 32 bytes = one AVX register
};
static_assert(sizeof(PriceBuffer) == 32, "size wrong");

// ── PART 4: std::hardware_destructive_interference_size (C++17) ────────
// The standard way to get the cache-line size portably.
// Equivalent to 64 on x86 / Apple Silicon.
static constexpr std::size_t CACHE_LINE =
    std::hardware_destructive_interference_size;

template <typename T>
struct CacheAligned {
    alignas(CACHE_LINE) T value{};
    // Pads to prevent any adjacent data from sharing the cache line.
    static_assert(sizeof(T) <= CACHE_LINE,
                  "T too large to fit in one cache line");
};

// ── PART 5: packed structs for wire protocols ─────────────────────────
// __attribute__((packed)) removes all padding.
// Do NOT use for regular objects — unaligned loads are slow on x86 and
// undefined behaviour on strict-alignment architectures.
// Use ONLY for serialisation (network/disk) buffers that you then copy.

struct [[gnu::packed]] WireQuote {
    std::uint32_t seq;
    std::uint16_t venue_id;
    double        bid_px;
    double        ask_px;
    std::uint32_t bid_sz;
    std::uint32_t ask_sz;
};
static_assert(sizeof(WireQuote) == 26,
              "wire format must be exactly 26 bytes");

int main()
{
    std::puts("── struct size comparison ────────────────────────");
    SHOW(PaddedBad);
    SHOW(PaddedGood);
    std::puts("");
    SHOW(OrderBad);
    SHOW(OrderGood);
    std::puts("");
    SHOW(CoreAccumulator);
    SHOW(PriceBuffer);
    SHOW(WireQuote);

    std::puts("\n── cache line size (hardware_destructive) ────────");
    std::printf("  std::hardware_destructive_interference_size = %zu\n",
                CACHE_LINE);

    std::puts("\n── CacheAligned<double> demo ────────────────────");
    CacheAligned<double> a, b;
    auto* pa = reinterpret_cast<char*>(&a);
    auto* pb = reinterpret_cast<char*>(&b);
    // In an array, sequential CacheAligned<double> are 64 bytes apart.
    CacheAligned<double> arr[2];
    auto* p0 = reinterpret_cast<char*>(&arr[0]);
    auto* p1 = reinterpret_cast<char*>(&arr[1]);
    std::printf("  arr[0] addr: %p\n", static_cast<void*>(p0));
    std::printf("  arr[1] addr: %p  (diff = %td = CACHE_LINE: %s)\n",
                static_cast<void*>(p1), p1 - p0,
                (p1 - p0) == static_cast<std::ptrdiff_t>(CACHE_LINE)
                    ? "yes" : "no");
    (void)pa; (void)pb;

    std::puts("\n── member offsets of OrderGood ──────────────────");
    std::printf("  price:   offset %zu\n", offsetof(OrderGood, price));
    std::printf("  symbol:  offset %zu\n", offsetof(OrderGood, symbol));
    std::printf("  qty:     offset %zu\n", offsetof(OrderGood, qty));
    std::printf("  is_buy:  offset %zu\n", offsetof(OrderGood, is_buy));
    std::printf("  ioc:     offset %zu\n", offsetof(OrderGood, ioc));
}
