#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/wait.h>

// ─────────────────────────────────────────────────────────────────────
// File locking: flock, fcntl byte-range locks, PID lock files.
//
// POSIX file locks are ADVISORY — only cooperating processes honour
// them. The kernel does NOT block raw read/write from other processes
// that don't call flock/fcntl.
//
// ── flock(fd, operation) — whole-file, BSD-style ────────────────────
//   LOCK_SH  — shared (read) lock; many holders allowed simultaneously
//   LOCK_EX  — exclusive (write) lock; one holder at a time
//   LOCK_UN  — release the lock
//   LOCK_NB  — non-blocking flag (OR it with SH/EX); returns -1 with
//               errno == EWOULDBLOCK if the lock is unavailable
//
//   flock locks are per (open-file-description, process) — duplicated
//   fds from dup() or fork() share the same lock.
//
// ── fcntl byte-range locks (POSIX record locks) ─────────────────────
//   More granular than flock: can lock any byte range.
//   struct flock (different from sys/file.h flock()) fields:
//     l_type   — F_RDLCK | F_WRLCK | F_UNLCK
//     l_whence — SEEK_SET | SEEK_CUR | SEEK_END (like lseek)
//     l_start  — byte offset
//     l_len    — length (0 = to end of file)
//     l_pid    — filled by F_GETLK with the blocking process's PID
//
//   F_SETLK  — set/clear lock non-blocking
//   F_SETLKW — set/clear lock blocking (wait)
//   F_GETLK  — test: does another process hold a conflicting lock?
//
//   Important: a process releases ALL its fcntl locks on a file when
//   ANY fd for that file is closed — even an unrelated fd.
//   This makes fcntl locks tricky in multi-threaded programs.
//
// ── PID lock file ────────────────────────────────────────────────────
//   Pattern:
//     open(path, O_CREAT | O_EXCL | O_RDWR, 0600)
//     write(fd, "<pid>\n")
//     — if EEXIST, another instance is running (read the PID to check)
//     — unlink on clean exit; crash-safe if we additionally use flock
//   Real pattern (crash-safe): O_RDWR | O_CREAT, then flock(LOCK_EX|NB).
//
// Compile:  g++ -std=c++20 09_file_locking.cpp -o file_locking && ./file_locking
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── PART 1: flock shared/exclusive ────────────────────────────────────
static void demo_flock()
{
    std::puts("── flock: shared and exclusive ──────────────────");
    const char* path = "/tmp/rates.dat";

    int fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open flock");
    ::write(fd, "data", 4);

    // Acquire an exclusive (write) lock.
    if (::flock(fd, LOCK_EX) == -1) die("flock EX");
    std::puts("  acquired LOCK_EX");

    // Non-blocking attempt from a child — should fail immediately.
    pid_t child = ::fork();
    if (child == -1) die("fork flock");

    if (child == 0) {
        // Open a SEPARATE fd so the child has its own flock state.
        int fd2 = ::open(path, O_RDONLY);
        if (fd2 == -1) die("open child");
        int r = ::flock(fd2, LOCK_SH | LOCK_NB);
        if (r == -1 && errno == EWOULDBLOCK)
            std::puts("  child: LOCK_SH | LOCK_NB → EWOULDBLOCK (expected)");
        else if (r == 0)
            std::puts("  child: acquired LOCK_SH");
        ::close(fd2);
        std::exit(0);
    }

    int status;
    ::waitpid(child, &status, 0);

    // Release; now a second child can take a shared lock.
    ::flock(fd, LOCK_UN);
    std::puts("  released LOCK_EX");

    pid_t c2 = ::fork();
    if (c2 == -1) die("fork flock2");
    if (c2 == 0) {
        int fd3 = ::open(path, O_RDONLY);
        if (fd3 == -1) die("open c2");
        ::flock(fd3, LOCK_SH);
        std::puts("  child2: acquired LOCK_SH after release");
        ::flock(fd3, LOCK_UN);
        ::close(fd3);
        std::exit(0);
    }
    ::waitpid(c2, &status, 0);

    ::close(fd);
    ::unlink(path);
}

// ── PART 2: fcntl byte-range record locks ─────────────────────────────
static void demo_fcntl_locks()
{
    std::puts("\n── fcntl byte-range locks ────────────────────────");
    const char* path = "/tmp/book.dat";

    // Simulate a flat binary file: 4 records of 16 bytes each.
    // We lock individual records to allow concurrent readers/writers.
    const int REC_SIZE = 16;
    const int N_RECS   = 4;

    int fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open fcntl");
    char blank[REC_SIZE]{};
    for (int i = 0; i < N_RECS; ++i) ::write(fd, blank, REC_SIZE);

    // Lock record 2 (bytes 32–47) exclusively.
    struct ::flock lk{};
    lk.l_type   = F_WRLCK;
    lk.l_whence = SEEK_SET;
    lk.l_start  = 2 * REC_SIZE;
    lk.l_len    = REC_SIZE;
    if (::fcntl(fd, F_SETLK, &lk) == -1) die("F_SETLK");
    std::printf("  locked record 2 (bytes %d–%d) F_WRLCK\n",
                2 * REC_SIZE, 3 * REC_SIZE - 1);

    // F_GETLK: ask the kernel who holds a conflicting lock.
    // We check as if another process wanted a write lock on the same range.
    struct ::flock test{};
    test.l_type   = F_WRLCK;
    test.l_whence = SEEK_SET;
    test.l_start  = 2 * REC_SIZE;
    test.l_len    = REC_SIZE;
    if (::fcntl(fd, F_GETLK, &test) == -1) die("F_GETLK");
    if (test.l_type == F_UNLCK)
        std::puts("  F_GETLK: no conflict (this fd owns it)");
    else
        std::printf("  F_GETLK: conflict — held by PID %d\n", (int)test.l_pid);

    // Lock a non-overlapping record (read lock on record 0).
    struct ::flock rlk{};
    rlk.l_type   = F_RDLCK;
    rlk.l_whence = SEEK_SET;
    rlk.l_start  = 0;
    rlk.l_len    = REC_SIZE;
    if (::fcntl(fd, F_SETLK, &rlk) == -1) die("F_RDLCK");
    std::puts("  locked record 0 F_RDLCK (non-overlapping — succeeds)");

    // Unlock record 2.
    lk.l_type = F_UNLCK;
    ::fcntl(fd, F_SETLK, &lk);
    std::puts("  unlocked record 2");

    ::close(fd);
    ::unlink(path);
}

// ── PART 3: PID lock file — single-instance trading process ───────────
// Crash-safe variant: open + flock (OS releases the lock on crash).
static void demo_pid_lockfile()
{
    std::puts("\n── PID lock file ─────────────────────────────────");
    const char* lock_path = "/tmp/pricer.pid.lock";
    ::unlink(lock_path); // clean from previous run

    // Open O_RDWR|O_CREAT — creates if absent; does NOT fail if present.
    int fd = ::open(lock_path, O_RDWR | O_CREAT, 0600);
    if (fd == -1) die("open pid lock");

    // Non-blocking exclusive flock — fails if another instance holds it.
    if (::flock(fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            // Read the PID from the file to report who is running.
            char pid_buf[16]{};
            ::pread(fd, pid_buf, sizeof(pid_buf) - 1, 0);
            std::fprintf(stderr, "  another instance is running (PID %s)\n",
                         pid_buf);
            ::close(fd);
            return;
        }
        die("flock pid lock");
    }

    // Truncate + write our PID.
    ::ftruncate(fd, 0);
    char pid_buf[32];
    int  n = std::snprintf(pid_buf, sizeof(pid_buf), "%d\n",
                           (int)::getpid());
    ::write(fd, pid_buf, static_cast<std::size_t>(n));
    std::printf("  lock acquired — PID %d wrote to %s\n",
                (int)::getpid(), lock_path);

    // Simulate a second process trying to start.
    pid_t child = ::fork();
    if (child == -1) die("fork pidlock");
    if (child == 0) {
        int fd2 = ::open(lock_path, O_RDWR | O_CREAT, 0600);
        int r   = ::flock(fd2, LOCK_EX | LOCK_NB);
        if (r == -1 && errno == EWOULDBLOCK) {
            char existing[16]{};
            ::pread(fd2, existing, sizeof(existing) - 1, 0);
            existing[strcspn(existing, "\n")] = '\0';
            std::printf("  second instance refused — held by PID %s\n",
                        existing);
        }
        ::close(fd2);
        std::exit(0);
    }
    int status;
    ::waitpid(child, &status, 0);

    // Release on clean exit.
    ::flock(fd, LOCK_UN);
    ::close(fd);
    ::unlink(lock_path);
    std::puts("  lock released on clean exit");
}

int main()
{
    demo_flock();
    demo_fcntl_locks();
    demo_pid_lockfile();
}
