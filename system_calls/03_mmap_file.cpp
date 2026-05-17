#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────
// Memory-mapped file I/O: mmap, munmap, madvise, msync, ftruncate.
//
// mmap(addr, len, prot, flags, fd, offset)
//   → void* pointing directly into the file's page-cache pages, or
//     MAP_FAILED on error.
//   addr    — nullptr lets the kernel choose
//   len     — bytes to map; must be ≤ file size for MAP_PRIVATE reads
//   prot    — PROT_READ | PROT_WRITE | PROT_NONE
//   flags   — MAP_PRIVATE (CoW, writes not flushed to disk)
//             MAP_SHARED  (writes visible to other mappings & file)
//   fd      — file descriptor (may be closed after mmap)
//   offset  — must be a multiple of the page size (usually 4096)
//
// Why mmap beats read() for large market data files:
//   • Zero-copy: no kernel→user buffer transfer; you see the page cache.
//   • Random access: pointer arithmetic instead of lseek + read pairs.
//   • Multiple processes mapping the same file share physical RAM pages.
//   • OS manages prefetching and eviction transparently.
//
// madvise hints:
//   MADV_SEQUENTIAL  — reading start-to-end; kernel prefetches ahead
//   MADV_RANDOM      — random lookups; disable read-ahead
//   MADV_DONTNEED    — pages no longer needed; OS may reclaim them
//
// msync(addr, len, flags)
//   MS_SYNC  — wait until dirty pages are flushed to disk
//   MS_ASYNC — schedule flush and return immediately
//
// ftruncate(fd, size): set file length BEFORE mapping for writable files.
//
// Compile:  g++ -std=c++20 03_mmap_file.cpp -o mmap_file && ./mmap_file
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// Tick record layout written into the file.
struct __attribute__((packed)) Tick {
    std::int64_t  timestamp_ns;  // nanoseconds since epoch
    double        price;
    std::int32_t  qty;
};

// ── Write a sample tick file using normal write() ─────────────────────
static void create_tick_file(const char* path, int n_ticks)
{
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open tick");
    for (int i = 0; i < n_ticks; ++i) {
        Tick t{ 1700000000'000000000LL + i * 100'000LL,
                100.0 + i * 0.01,
                100 + (i % 50) };
        ::write(fd, &t, sizeof(t));
    }
    ::close(fd);
}

int main()
{
    const char* path    = "/tmp/ticks.bin";
    const int   n_ticks = 1000;
    create_tick_file(path, n_ticks);

    // ── MAP_PRIVATE read: scan the tick file ──────────────────────────
    // Open read-only; fstat to get the size; mmap with MAP_PRIVATE.
    int fd = ::open(path, O_RDONLY);
    if (fd == -1) die("open for mmap");

    struct stat sb;
    if (::fstat(fd, &sb) == -1) die("fstat");
    std::size_t len = static_cast<std::size_t>(sb.st_size);

    // Hint: sequential scan — kernel should read-ahead.
    void* raw = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (raw == MAP_FAILED) die("mmap read");
    ::close(fd); // fd may be closed immediately after mmap

    ::madvise(raw, len, MADV_SEQUENTIAL);

    const auto* ticks = static_cast<const Tick*>(raw);
    std::size_t  n     = len / sizeof(Tick);

    double      vwap     = 0.0;
    std::int64_t vol_sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        vwap    += ticks[i].price * ticks[i].qty;
        vol_sum += ticks[i].qty;
    }
    if (vol_sum > 0) vwap /= static_cast<double>(vol_sum);

    std::printf("── MAP_PRIVATE scan ──────────────────────────────\n");
    std::printf("  ticks=%zu  vol=%lld  VWAP=%.4f\n",
                n, (long long)vol_sum, vwap);

    // Tell the OS we're done — pages can be reclaimed.
    ::madvise(raw, len, MADV_DONTNEED);
    ::munmap(raw, len);

    // ── MAP_SHARED writable: in-place price update ────────────────────
    // ftruncate is required to set the file size before mapping RW.
    // (For an existing file of the right size this is a no-op; shown for
    //  completeness when creating a new writable file.)
    int rw_fd = ::open(path, O_RDWR);
    if (rw_fd == -1) die("open rdwr");

    void* wraw = ::mmap(nullptr, len, PROT_READ | PROT_WRITE,
                        MAP_SHARED, rw_fd, 0);
    if (wraw == MAP_FAILED) die("mmap shared");
    ::close(rw_fd);

    auto* wticks = static_cast<Tick*>(wraw);
    // Apply a hypothetical end-of-day price adjustment factor.
    const double adj = 1.0001;
    for (std::size_t i = 0; i < n; ++i)
        wticks[i].price *= adj;

    // msync MS_SYNC — block until dirty pages are flushed to disk.
    if (::msync(wraw, len, MS_SYNC) == -1) die("msync");
    ::munmap(wraw, len);

    // ── Verify the update survived ────────────────────────────────────
    int vfd = ::open(path, O_RDONLY);
    if (vfd == -1) die("open verify");
    ::fstat(vfd, &sb);
    void* vraw = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, vfd, 0);
    if (vraw == MAP_FAILED) die("mmap verify");
    ::close(vfd);

    const auto* vticks = static_cast<const Tick*>(vraw);
    std::printf("\n── MAP_SHARED verify ─────────────────────────────\n");
    std::printf("  first tick price after msync: %.4f  (expected ~%.4f)\n",
                vticks[0].price, 100.0 * adj);
    std::printf("  last  tick price after msync: %.4f\n",
                vticks[n - 1].price);
    ::munmap(vraw, len);

    // ── madvise MADV_RANDOM: simulate random order-book lookups ───────
    int rnd_fd = ::open(path, O_RDONLY);
    if (rnd_fd == -1) die("open random");
    ::fstat(rnd_fd, &sb);
    void* rndraw = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, rnd_fd, 0);
    if (rndraw == MAP_FAILED) die("mmap random");
    ::close(rnd_fd);

    ::madvise(rndraw, len, MADV_RANDOM);
    const auto* rt = static_cast<const Tick*>(rndraw);
    // Simulate out-of-order lookups (e.g. binary-searching by price).
    double sum = 0.0;
    for (int s = 0; s < 5; ++s) {
        std::size_t idx = static_cast<std::size_t>(s) * 200;
        sum += rt[idx].price;
    }
    std::printf("\n── MADV_RANDOM spot check sum: %.4f\n", sum);

    ::munmap(rndraw, len);
    ::unlink(path);
}
