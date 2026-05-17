#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <new>         // std::launder

// ─────────────────────────────────────────────────────────────────────
// Shared-memory IPC: MAP_ANONYMOUS|MAP_SHARED and shm_open/shm_unlink.
//
// Two ways to share memory between processes without a file:
//
//   1) MAP_ANONYMOUS | MAP_SHARED + fork()
//      — mapping created in parent; child inherits it.
//      — simplest; limited to the parent/child family.
//
//   2) shm_open(name, ...) + mmap()
//      — any process that knows the name can open the segment.
//      — behaves like a file in /dev/shm (Linux) or a tmpfs fd.
//      — shm_unlink() removes the name; memory freed when all fds closed.
//
// Synchronisation is YOUR responsibility.
// Use std::atomic for single-value lock-free updates.
// Use a pthread_mutex with PTHREAD_PROCESS_SHARED for multi-field updates.
//
// Layout:
//   struct PriceUpdate { atomic<int> seq; atomic<double> price; }
//   Writer increments seq (odd → writing; even → done).
//   Reader spins until seq is even and stable (seqlock pattern).
//
// Compile:  g++ -std=c++20 04_mmap_ipc.cpp -o mmap_ipc && ./mmap_ipc
//           On Linux also link: -lrt
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Shared payload ────────────────────────────────────────────────────
// All fields are std::atomic so they are safe to access from multiple
// processes without any OS-level mutex.
struct PriceUpdate {
    std::atomic<int>    seq{0};    // seqlock counter (even = stable)
    std::atomic<double> mid{0.0};  // mid-price
    std::atomic<double> spread{0.0};
    std::atomic<int>    n_updates{0};
};

// ── PART 1: anonymous shared mapping (parent + children only) ─────────
static void demo_anonymous_shared()
{
    std::puts("── Anonymous shared mapping (fork) ──────────────");

    // MAP_ANONYMOUS | MAP_SHARED: zeroed pages shared across fork().
    void* raw = ::mmap(nullptr, sizeof(PriceUpdate),
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (raw == MAP_FAILED) die("mmap anonymous");

    // Construct atomic members in the shared page.
    auto* pu = new (raw) PriceUpdate{};

    pid_t child = ::fork();
    if (child == -1) die("fork");

    if (child == 0) {
        // ── Child: writer — publishes 5 price updates ─────────────────
        for (int i = 0; i < 5; ++i) {
            int s = pu->seq.load(std::memory_order_acquire);
            pu->seq.store(s + 1, std::memory_order_release); // mark start
            pu->mid.store(100.0 + i * 0.5, std::memory_order_release);
            pu->spread.store(0.02 + i * 0.001, std::memory_order_release);
            pu->n_updates.fetch_add(1, std::memory_order_release);
            pu->seq.store(s + 2, std::memory_order_release); // mark done
            ::usleep(1000); // ~1 ms between updates
        }
        std::exit(0);
    }

    // ── Parent: reader — polls until 5 updates consumed ──────────────
    for (int seen = 0; seen < 5; ) {
        int s1 = pu->seq.load(std::memory_order_acquire);
        if (s1 % 2 != 0) { ::sched_yield(); continue; } // writer mid-update
        double mid    = pu->mid.load(std::memory_order_acquire);
        double spread = pu->spread.load(std::memory_order_acquire);
        int    s2     = pu->seq.load(std::memory_order_acquire);
        if (s1 != s2)   { ::sched_yield(); continue; } // torn read
        int nu = pu->n_updates.load(std::memory_order_acquire);
        if (nu > seen) {
            std::printf("  reader: seq=%d  mid=%.3f  spread=%.4f\n",
                        nu, mid, spread);
            seen = nu;
        }
        ::sched_yield();
    }

    int status;
    ::waitpid(child, &status, 0);
    pu->~PriceUpdate();
    ::munmap(raw, sizeof(PriceUpdate));
}

// ── PART 2: named shared memory (shm_open — any process by name) ──────
static const char* SHM_NAME = "/applied_cpp_prices";

static void demo_named_shm_writer()
{
    // Create and size the shared memory object.
    int fd = ::shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) die("shm_open writer");
    if (::ftruncate(fd, sizeof(PriceUpdate)) == -1) die("ftruncate");

    void* raw = ::mmap(nullptr, sizeof(PriceUpdate),
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd); // fd not needed after mmap
    if (raw == MAP_FAILED) die("mmap shm writer");

    auto* pu = new (raw) PriceUpdate{};

    // Publish 3 snapshots.
    for (int i = 0; i < 3; ++i) {
        int s = pu->seq.load(std::memory_order_acquire);
        pu->seq.store(s + 1, std::memory_order_release);
        pu->mid.store(200.0 + i * 1.0, std::memory_order_release);
        pu->spread.store(0.10 + i * 0.01, std::memory_order_release);
        pu->n_updates.fetch_add(1, std::memory_order_release);
        pu->seq.store(s + 2, std::memory_order_release);
        ::usleep(5000);
    }

    pu->~PriceUpdate();
    ::munmap(raw, sizeof(PriceUpdate));
    // Writer removes the name so no new openers can connect.
    ::shm_unlink(SHM_NAME);
}

static void demo_named_shm_reader()
{
    int fd = ::shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd == -1) {
        // Writer hasn't created it yet; wait briefly.
        ::usleep(20000);
        fd = ::shm_open(SHM_NAME, O_RDONLY, 0);
        if (fd == -1) die("shm_open reader");
    }

    void* raw = ::mmap(nullptr, sizeof(PriceUpdate),
                       PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (raw == MAP_FAILED) die("mmap shm reader");

    const auto* pu = static_cast<const PriceUpdate*>(raw);

    // Read until the writer has published 3 snapshots.
    for (int seen = 0; seen < 3; ) {
        int nu = pu->n_updates.load(std::memory_order_acquire);
        if (nu > seen) {
            double mid    = pu->mid.load(std::memory_order_acquire);
            double spread = pu->spread.load(std::memory_order_acquire);
            std::printf("  reader (shm): update=%d  mid=%.3f  spread=%.4f\n",
                        nu, mid, spread);
            seen = nu;
        }
        ::usleep(1000);
    }

    ::munmap(raw, sizeof(PriceUpdate));
}

static void demo_named_shm()
{
    std::puts("\n── Named shared memory (shm_open) ───────────────");
    ::shm_unlink(SHM_NAME); // clean up from any previous run

    pid_t child = ::fork();
    if (child == -1) die("fork shm");

    if (child == 0) {
        demo_named_shm_writer();
        std::exit(0);
    }

    demo_named_shm_reader();

    int status;
    ::waitpid(child, &status, 0);
    std::puts("  shm demo complete.");
}

int main()
{
    demo_anonymous_shared();
    demo_named_shm();
}
