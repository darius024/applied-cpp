#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory_resource>
#include <new>
#include <vector>
#include <array>

// ─────────────────────────────────────────────────────────────────────
// Memory allocators: pmr, monotonic buffer, pool, arena, vs malloc.
//
// Why the default allocator (malloc/new) is slow on the hot path:
//   - Thread-synchronisation overhead (internal lock or CAS).
//   - Free-list search for a suitable block (O(n) worst case).
//   - Possible syscall (brk/mmap) when the pool is exhausted.
//   - Poor cache behaviour: freed/reused blocks may be cold.
//   Typical measured cost: 50–200 ns per allocation.
//
// std::pmr (C++17 — Polymorphic Memory Resources)
//   The standard library's plug-in allocator system.
//   All pmr containers (pmr::vector, pmr::string, ...) take a
//   memory_resource* at construction time.
//
//   std::pmr::monotonic_buffer_resource
//     - Bump-pointer allocator from a user-supplied buffer.
//     - O(1) allocation, O(1) deallocation (no-op; frees only on reset).
//     - Falls back to an upstream allocator when the buffer is full.
//     - Perfect for per-event scratch space: reset() at event boundary.
//
//   std::pmr::unsynchronized_pool_resource
//     - Maintains free-lists of fixed-size blocks.
//     - O(1) alloc/dealloc for same-size objects.
//     - Single-threaded only.
//
//   std::pmr::synchronized_pool_resource
//     - Same with a mutex; thread-safe.
//
// Arena allocator (manual)
//   A simple bump-pointer into a large buffer; can also support
//   aligned sub-allocations and checkpoint/rollback.
//   Favoured in HFT order handlers: allocate cheaply during event
//   processing, reset the arena at the end of the event.
//
// Compile:  g++ -std=c++20 -O2 07_allocators.cpp -o alloc && ./alloc
// ─────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static double elapsed_ns(std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b)
{
    return static_cast<double>(
               std::chrono::duration_cast<NS>(b - a).count());
}

// ── Simple arena allocator ────────────────────────────────────────────
// Minimal bump-pointer arena; not thread-safe.
class Arena {
public:
    explicit Arena(std::size_t capacity)
        : buf_(new std::byte[capacity]), cap_(capacity), used_(0) {}

    void* alloc(std::size_t bytes, std::size_t align = alignof(std::max_align_t))
    {
        // Round up 'used_' to the required alignment.
        std::size_t aligned = (used_ + align - 1) & ~(align - 1);
        if (aligned + bytes > cap_) return nullptr; // OOM
        void* ptr = buf_.get() + aligned;
        used_ = aligned + bytes;
        return ptr;
    }

    template <typename T, typename... Args>
    T* make(Args&&... args)
    {
        void* p = alloc(sizeof(T), alignof(T));
        if (!p) return nullptr;
        return ::new(p) T{std::forward<Args>(args)...};
    }

    void reset() noexcept { used_ = 0; }

    std::size_t used()      const noexcept { return used_; }
    std::size_t capacity()  const noexcept { return cap_; }

private:
    std::unique_ptr<std::byte[]> buf_;
    std::size_t cap_;
    std::size_t used_;
};

// ── PART 1: monotonic_buffer_resource ─────────────────────────────────
static void demo_monotonic()
{
    std::puts("── pmr::monotonic_buffer_resource ───────────────");

    // Use a stack buffer; falls back to heap when exhausted.
    std::array<std::byte, 8192> stack_buf;
    std::pmr::monotonic_buffer_resource pool{
        stack_buf.data(), stack_buf.size(),
        std::pmr::null_memory_resource() // no fallback — crash on OOM
    };

    // pmr::vector doesn't call malloc — it draws from our buffer.
    {
        std::pmr::vector<double> prices{&pool};
        prices.reserve(128);
        for (int i = 0; i < 100; ++i)
            prices.push_back(100.0 + i * 0.01);

        std::printf("  vector of %zu prices (all from stack buffer)\n",
                    prices.size());
        std::printf("  buffer capacity: %zu bytes\n", stack_buf.size());
    }
    // No heap allocation happened; pool.release() would reset.
    std::puts("  no malloc called — O(1) bump-pointer allocations");
}

// ── PART 2: unsynchronized_pool_resource ─────────────────────────────
static void demo_pool()
{
    std::puts("\n── pmr::unsynchronized_pool_resource ────────────");

    std::pmr::unsynchronized_pool_resource pool{
        std::pmr::pool_options{ .max_blocks_per_chunk = 256,
                                 .largest_required_pool_block = 512 },
        std::pmr::new_delete_resource()
    };

    // Allocate and free many fixed-size objects — free-list reuse.
    const int N = 1000;
    std::vector<void*> ptrs;
    ptrs.reserve(N);

    for (int i = 0; i < N; ++i)
        ptrs.push_back(pool.allocate(64, 8)); // 64-byte blocks

    // After deallocation, blocks go back to the pool free-list.
    for (void* p : ptrs)
        pool.deallocate(p, 64, 8);

    // Second allocation round: served from free-list — O(1), no syscall.
    for (int i = 0; i < N; ++i)
        ptrs[static_cast<std::size_t>(i)] = pool.allocate(64, 8);
    for (void* p : ptrs)
        pool.deallocate(p, 64, 8);

    std::puts("  pool allocator: second round served from free-list");
}

// ── PART 3: Arena — per-event scratch allocation ──────────────────────
struct PriceUpdate {
    std::int64_t ts;
    double       price;
    int          qty;
};

static void demo_arena()
{
    std::puts("\n── Arena allocator (per-event pattern) ──────────");

    Arena arena{65536}; // 64 KiB scratch space

    // Simulate: process 1000 events; each event allocates scratch objects
    // and resets the arena at the end.
    const int EVENTS = 1000;
    const int OBJ_PER_EVENT = 50;
    volatile double sink = 0.0;

    auto t0 = Clock::now();
    for (int e = 0; e < EVENTS; ++e) {
        arena.reset(); // O(1) — bump pointer back to 0

        for (int i = 0; i < OBJ_PER_EVENT; ++i) {
            auto* pu = arena.make<PriceUpdate>(
                PriceUpdate{1'700'000'000LL + e, 100.0 + i * 0.01, 100});
            sink = pu->price;
        }
    }
    auto t1 = Clock::now();

    double ns_per_alloc = elapsed_ns(t0, t1)
                          / (EVENTS * static_cast<double>(OBJ_PER_EVENT));
    std::printf("  arena alloc: %.2f ns/object  (EVENTS=%d OBJ/event=%d)\n",
                ns_per_alloc, EVENTS, OBJ_PER_EVENT);
    (void)sink;
}

// ── PART 4: comparison — arena vs new/delete ───────────────────────────
static void demo_comparison()
{
    std::puts("\n── arena vs new/delete per-object comparison ────");

    const int N    = 50'000;
    const int REPS = 8;
    volatile double sink = 0.0;

    // new/delete
    auto t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        std::vector<PriceUpdate*> ptrs;
        ptrs.reserve(N);
        for (int i = 0; i < N; ++i)
            ptrs.push_back(new PriceUpdate{0, 100.0 + i, 100});
        for (auto* p : ptrs) { sink = p->price; delete p; }
    }
    auto t1 = Clock::now();
    double heap_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    // Arena
    Arena arena2{static_cast<std::size_t>(N + 16) * sizeof(PriceUpdate)};
    t0 = Clock::now();
    for (int r = 0; r < REPS; ++r) {
        arena2.reset();
        for (int i = 0; i < N; ++i) {
            auto* p = arena2.make<PriceUpdate>(
                PriceUpdate{0, 100.0 + i, 100});
            sink = p->price;
        }
    }
    t1 = Clock::now();
    double arena_ns = elapsed_ns(t0, t1) / (REPS * static_cast<double>(N));

    std::printf("  new/delete:  %.2f ns/object\n", heap_ns);
    std::printf("  arena:       %.2f ns/object\n", arena_ns);
    std::printf("  speedup:     %.1fx\n", heap_ns / arena_ns);
    (void)sink;
}

int main()
{
    demo_monotonic();
    demo_pool();
    demo_arena();
    demo_comparison();
}
