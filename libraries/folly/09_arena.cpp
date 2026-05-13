#include <folly/memory/Arena.h>
#include <folly/memory/SysArena.h>
#include <iostream>
#include <string>
#include <vector>

// compile: g++ -std=c++17 09_arena.cpp -o 09_arena -lfolly -lglog -lgflags -pthread

// ─────────────────────────────────────────────────────────────────────
// folly::Arena / SysArena: bump pointer allocators.
//
// Arena<Alloc>:
//   Allocates from a backing allocator in large blocks. Individual
//   allocate(size) calls are O(1) pointer bumps. There is no per-object
//   free() — the entire arena is released at once when it goes out of
//   scope (or on clear()).
//
// SysArena:
//   Same semantics but sources blocks from mmap. Useful when you don't
//   already have an upstream allocator.
//
// ArenaAllocator<T>:
//   An STL allocator wrapper around Arena/SysArena. Lets you use arena
//   memory with std::vector, std::string, etc. without changing the
//   container's interface.
//
// HPC relevance:
//   - Request-scoped arenas in servers: allocate freely during a
//     request, free everything in one shot at the end — no fragmentation,
//     no per-object bookkeeping.
//   - Parse trees, ASTs, graph nodes: built once, read many times,
//     then destroyed together.
//   - Eliminates allocator overhead in tight loops where objects are
//     short-lived and uniformly scoped.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── SysArena: mmap-backed ─────────────────────────────────────────
    {
        folly::SysArena arena;

        // allocate returns void*; alignment defaults to sizeof(void*)
        int*    pi  = static_cast<int*>   (arena.allocate(sizeof(int)));
        double* pd  = static_cast<double*>(arena.allocate(sizeof(double)));
        *pi = 42;
        *pd = 3.14;

        std::cout << "arena int: "    << *pi << "\n";
        std::cout << "arena double: " << *pd << "\n";

        // Allocate a raw char buffer (e.g. for a serialised message)
        const char* msg = "hello arena";
        char* buf = static_cast<char*>(arena.allocate(std::strlen(msg) + 1));
        std::memcpy(buf, msg, std::strlen(msg) + 1);
        std::cout << "arena string: " << buf << "\n";

        // arena goes out of scope → all allocations freed in one munmap
    }

    // ── ArenaAllocator with std::vector ──────────────────────────────
    {
        folly::SysArena arena;
        using IntAlloc = folly::SysArena::Alloc<int>;

        // All vector storage comes from the arena — no heap new/delete
        std::vector<int, IntAlloc> v(IntAlloc(&arena));
        v.reserve(16);
        for (int i = 0; i < 10; ++i) v.push_back(i);

        int sum = 0;
        for (int x : v) sum += x;
        std::cout << "arena vector sum: " << sum << "\n"; // 45
    }

    // ── Multiple objects in one arena (request-scope pattern) ─────────
    {
        folly::SysArena arena;

        // Simulate allocating several objects for one HTTP request
        struct Header { int code; char data[32]; };
        struct Body   { std::size_t len; char data[128]; };

        auto* h = new (arena.allocate(sizeof(Header))) Header{200, "OK"};
        auto* b = new (arena.allocate(sizeof(Body)))   Body{5, "hello"};

        std::cout << "request header code: " << h->code << "\n";
        std::cout << "request body: "        << b->data << "\n";

        // Both freed instantly when arena destructs — O(1) regardless of
        // how many objects were allocated.
    }
}
