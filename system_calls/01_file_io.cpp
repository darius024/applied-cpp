#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// POSIX file I/O: open, read, write, lseek, dup2.
//
// A file descriptor (fd) is a small non-negative integer — the kernel's
// handle to an open file, pipe, socket, or device.
// 0=stdin  1=stdout  2=stderr are always open at program start.
//
// open(path, flags [, mode]) — returns a new fd or -1 on error.
//   Mandatory (pick one): O_RDONLY | O_WRONLY | O_RDWR
//   Optional:
//     O_CREAT    — create if absent; requires mode (e.g. 0644)
//     O_TRUNC    — truncate to zero on open
//     O_APPEND   — all writes are atomic appends to end of file
//     O_EXCL     — fail with EEXIST if file already exists
//     O_CLOEXEC  — close this fd automatically across exec()
//     O_NONBLOCK — return EAGAIN instead of blocking
//
// read/write may return fewer bytes than requested (short read/write).
// Always loop; always check for -1 (error) and 0 (EOF on read).
// errno == EINTR means interrupted by a signal — retry.
//
// Compile:  g++ -std=c++20 01_file_io.cpp -o file_io && ./file_io
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// Full-write helper: loops until all bytes are written or an error occurs.
static void write_all(int fd, const void* buf, std::size_t len)
{
    const auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n == -1) {
            if (errno == EINTR) continue; // signal interrupted; retry
            die("write");
        }
        p   += n;
        len -= static_cast<std::size_t>(n);
    }
}

int main()
{
    const char* path = "/tmp/prices.csv";

    // ── Write: create or overwrite ────────────────────────────────────
    // O_WRONLY | O_CREAT | O_TRUNC — standard "write new file" pattern.
    // 0644 = owner rw, group r, others r.
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open for write");

    const char* header = "ts,price\n";
    write_all(fd, header, std::strlen(header));

    for (int i = 0; i < 5; ++i) {
        char line[64];
        int  len = std::snprintf(line, sizeof(line),
                                 "%d,%.2f\n", 1700000000 + i, 100.0 + i * 0.25);
        write_all(fd, line, static_cast<std::size_t>(len));
    }
    ::close(fd);

    // ── Read into a buffer ────────────────────────────────────────────
    fd = ::open(path, O_RDONLY);
    if (fd == -1) die("open for read");

    char buf[512]{};
    ssize_t total = 0;
    while (true) {
        ssize_t n = ::read(fd, buf + total,
                           sizeof(buf) - 1 - static_cast<std::size_t>(total));
        if (n == -1) { if (errno == EINTR) continue; die("read"); }
        if (n == 0) break; // EOF
        total += n;
    }
    std::cout << "── read " << total << " bytes ──────────────────────\n"
              << buf << "\n";

    // ── lseek: position the read cursor ──────────────────────────────
    // SEEK_SET=absolute, SEEK_CUR=relative, SEEK_END=from end.
    off_t pos = ::lseek(fd, 9, SEEK_SET); // skip "ts,price\n" header (9 bytes)
    if (pos == -1) die("lseek");

    char first_line[32]{};
    ::read(fd, first_line, sizeof(first_line) - 1);
    first_line[strcspn(first_line, "\n")] = '\0'; // strip newline
    std::cout << "── first data line: " << first_line << "\n\n";
    ::close(fd);

    // ── O_APPEND: atomic appends from multiple writers ────────────────
    // All writes go to end of file atomically — no lseek needed.
    // Safe for concurrent processes writing to a shared log file.
    int afd = ::open(path, O_WRONLY | O_APPEND);
    if (afd == -1) die("open append");
    const char* extra = "1700000005,101.25\n";
    write_all(afd, extra, std::strlen(extra));
    ::close(afd);
    std::cout << "── appended one extra line\n";

    // ── dup2: redirect stdout to a file ──────────────────────────────
    // dup2(old_fd, new_fd) makes new_fd a duplicate of old_fd.
    // After dup2(log_fd, 1), writes to fd 1 (stdout) go to the log file.
    int log_fd = ::open("/tmp/redirect.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd == -1) die("open log");

    int saved_stdout = ::dup(1); // save real stdout
    ::dup2(log_fd, 1);           // redirect stdout → log file
    ::write(1, "this line went to the log file\n", 31);
    ::dup2(saved_stdout, 1);     // restore stdout
    ::close(saved_stdout);
    ::close(log_fd);
    std::cout << "── stdout restored (dup2 demo complete)\n";

    // ── O_EXCL | O_CREAT: atomic lock-file creation ───────────────────
    // Fails with EEXIST if another process already holds the lock.
    // The OS releases the file when the process dies — crash-safe.
    const char* lock_path = "/tmp/pricer.lock";
    int lfd = ::open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (lfd == -1) {
        if (errno == EEXIST)
            std::cout << "── lock already held\n";
        else
            die("open lock");
    } else {
        char pid_buf[32];
        std::snprintf(pid_buf, sizeof(pid_buf), "%d\n",
                      static_cast<int>(::getpid()));
        write_all(lfd, pid_buf, std::strlen(pid_buf));
        ::close(lfd);
        std::cout << "── lock acquired (PID " << ::getpid() << ")\n";
        ::unlink(lock_path); // release on clean exit
    }

    ::unlink(path);
    ::unlink("/tmp/redirect.log");
}
