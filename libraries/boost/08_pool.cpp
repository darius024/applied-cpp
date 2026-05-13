#include <boost/pool/pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/pool/pool_allocator.hpp>
#include <boost/pool/fast_pool_allocator.hpp>
#include <vector>
#include <list>
#include <iostream>

// compile: g++ -std=c++17 08_pool.cpp -o 08_pool

// ─────────────────────────────────────────────────────────────────────
// boost::pool: segregated-storage memory pools. Header-only.
//
// pool<>               — allocates raw chunks of a fixed size.
//                        malloc-speed or better; O(1) alloc/free.
//
// object_pool<T>       — typed pool: construct() / destroy().
//                        All live objects destroyed when pool goes out of scope.
//
// pool_allocator<T>    — STL allocator backed by a pool; best for
//                        contiguous containers (vector).
//
// fast_pool_allocator<T> — per-size pool; best for node-based containers
//                          (list, map, set) where individual small nodes
//                          are allocated and freed frequently.
//
// HPC relevance: replaces global new/delete with a thread-unsafe but
// extremely fast allocator when allocation patterns are uniform (fixed-
// size objects, short-lived objects). Eliminates heap fragmentation.
// ─────────────────────────────────────────────────────────────────────

struct Point { double x, y, z; };

int main()
{
    // ── pool<>: raw fixed-size chunks ─────────────────────────────────
    {
        boost::pool<> raw(sizeof(Point));       // chunk size = sizeof(Point)
        void* p1 = raw.malloc();                // O(1), no system call after first block
        void* p2 = raw.malloc();
        raw.free(p1);                           // returned to pool, not to OS
        raw.free(p2);
        // pool destructor releases everything to the OS
        std::cout << "pool<>: allocated and freed 2 raw chunks\n";
    }

    // ── object_pool<T>: construct / destroy ───────────────────────────
    {
        boost::object_pool<Point> op;

        Point* a = op.construct(1.0, 2.0, 3.0); // placement-new into pool memory
        Point* b = op.construct(4.0, 5.0, 6.0);

        std::cout << "object_pool: a=(" << a->x << "," << a->y << "," << a->z << ")\n";

        op.destroy(a); // calls ~Point() and returns chunk to pool

        // b is still live; op destructor calls ~Point() on b automatically
    }

    // ── pool_allocator<T>: vector with pool backing ───────────────────
    {
        // The pool is shared globally per T/UserAllocator combination.
        std::vector<int, boost::pool_allocator<int>> v;
        v.reserve(128);
        for (int i = 0; i < 10; ++i) v.push_back(i);
        std::cout << "pool_allocator vector size: " << v.size() << "\n";
    }

    // ── fast_pool_allocator<T>: list with per-node pool ───────────────
    // std::list allocates one node at a time; fast_pool_allocator batches
    // node allocations from a segregated pool, removing per-node heap overhead.
    {
        std::list<int, boost::fast_pool_allocator<int>> lst;
        for (int i = 0; i < 10; ++i) lst.push_back(i);
        int sum = 0;
        for (int x : lst) sum += x;
        std::cout << "fast_pool_allocator list sum: " << sum << "\n"; // 45
    }
}
