#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// Anonymous pipes, named FIFOs, and socketpair.
//
// Anonymous pipe: pipe(pipefd[2]) or pipe2(pipefd, flags)
//   — unidirectional: pipefd[0] reads, pipefd[1] writes
//   — kernel buffer ~64 KiB; reads block when empty, writes block full
//   — EOF on read when ALL write ends are closed
//   — only shared between related processes (parent/child after fork)
//   — pipe2 with O_CLOEXEC is the modern form; avoids fd leaks to exec
//
// dup2(old_fd, new_fd): make new_fd a copy of old_fd.
//   Standard pattern for stdout redirection before exec:
//     dup2(pipefd[1], STDOUT_FILENO);
//     close(pipefd[1]);
//     execvp(...);
//
// socketpair(AF_UNIX, SOCK_STREAM, 0, sv[2])
//   — bidirectional full-duplex "pipe"
//   — both sv[0] and sv[1] can read AND write
//   — useful when parent and child both send and receive
//
// Named pipe (FIFO): mkfifo(path, mode)
//   — appears in the filesystem; any process can open it by name
//   — open() blocks until BOTH ends are connected (use O_NONBLOCK to
//     detect that no writer/reader is present)
//   — behaves like an anonymous pipe once both ends are open
//   — survives process exit; must be unlinked explicitly
//
// Comparison:
//   pipe        — related processes, unidirectional, ephemeral
//   socketpair  — related processes, bidirectional, ephemeral
//   FIFO        — unrelated processes, unidirectional, named in FS
//
// Compile:  g++ -std=c++20 07_pipes_fifo.cpp -o pipes && ./pipes
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

static void write_all(int fd, const void* buf, std::size_t len)
{
    const auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n <= 0) { if (errno == EINTR) continue; die("write_all"); }
        p += n; len -= static_cast<std::size_t>(n);
    }
}

// ── PART 1: anonymous pipe — parent sends order IDs, child echoes ─────
static void demo_anonymous_pipe()
{
    std::puts("── anonymous pipe ───────────────────────────────");
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) == -1) die("pipe2");

    pid_t child = ::fork();
    if (child == -1) die("fork pipe");

    if (child == 0) {
        ::close(pipefd[1]); // child is reader — close unused write end

        char buf[256]{};
        ssize_t total = 0;
        while (true) {
            ssize_t n = ::read(pipefd[0], buf + total,
                               sizeof(buf) - 1 - static_cast<std::size_t>(total));
            if (n == 0) break; // EOF
            if (n == -1) { if (errno == EINTR) continue; die("read pipe"); }
            total += n;
        }
        ::close(pipefd[0]);
        std::printf("  child received %zd bytes: %s", total, buf);
        std::exit(0);
    }

    ::close(pipefd[0]); // parent is writer — close unused read end
    const char* orders[] = { "ORD_001\n", "ORD_002\n", "ORD_003\n" };
    for (const char* o : orders)
        write_all(pipefd[1], o, std::strlen(o));
    ::close(pipefd[1]); // EOF to child

    int status;
    ::waitpid(child, &status, 0);
}

// ── PART 2: dup2 stdout redirect — capture child's stdout ─────────────
static void demo_dup2_stdout()
{
    std::puts("\n── dup2: redirect child stdout into pipe ─────────");
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) == -1) die("pipe2 dup2");

    pid_t child = ::fork();
    if (child == -1) die("fork dup2");

    if (child == 0) {
        // Redirect stdout (fd 1) to pipefd[1].
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]); // fd 1 is now the write end

        // execvp — child's stdout goes into the pipe.
        char* argv[] = { const_cast<char*>("uname"), nullptr };
        ::execvp("uname", argv);
        die("execvp");
    }

    ::close(pipefd[1]); // parent reads

    char out[64]{};
    ssize_t n = ::read(pipefd[0], out, sizeof(out) - 1);
    ::close(pipefd[0]);
    if (n > 0) {
        out[n] = '\0';
        std::printf("  uname output: %s", out);
    }

    int status;
    ::waitpid(child, &status, 0);
}

// ── PART 3: socketpair — bidirectional parent-child channel ───────────
static void demo_socketpair()
{
    std::puts("\n── socketpair (bidirectional) ────────────────────");
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) die("socketpair");

    pid_t child = ::fork();
    if (child == -1) die("fork socketpair");

    if (child == 0) {
        ::close(sv[0]); // child uses sv[1]
        // Echo back whatever we receive.
        char buf[64]{};
        ssize_t n = ::read(sv[1], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            char reply[80];
            int  rlen = std::snprintf(reply, sizeof(reply), "ECHO:%s", buf);
            write_all(sv[1], reply, static_cast<std::size_t>(rlen));
        }
        ::close(sv[1]);
        std::exit(0);
    }

    ::close(sv[1]); // parent uses sv[0]
    const char* req = "PRICE_REQUEST";
    write_all(sv[0], req, std::strlen(req));
    ::shutdown(sv[0], SHUT_WR); // signal end of send side

    char reply[80]{};
    ssize_t n = ::read(sv[0], reply, sizeof(reply) - 1);
    if (n > 0) {
        reply[n] = '\0';
        std::printf("  parent received: %s\n", reply);
    }
    ::close(sv[0]);

    int status;
    ::waitpid(child, &status, 0);
}

// ── PART 4: named FIFO — simulates a cross-process market data feed ───
static const char* FIFO_PATH = "/tmp/md_feed.fifo";

static void demo_fifo()
{
    std::puts("\n── named FIFO ────────────────────────────────────");
    ::unlink(FIFO_PATH); // clean slate

    if (::mkfifo(FIFO_PATH, 0600) == -1) die("mkfifo");

    // Writer and reader run in separate child processes to avoid deadlock
    // on open() (both sides must be connected before either unblocks).
    pid_t writer = ::fork();
    if (writer == -1) die("fork fifo writer");

    if (writer == 0) {
        // Writer: open FIFO for writing (blocks until reader connects).
        int fd = ::open(FIFO_PATH, O_WRONLY);
        if (fd == -1) die("open fifo write");
        for (int i = 0; i < 4; ++i) {
            char msg[32];
            int  len = std::snprintf(msg, sizeof(msg),
                                     "TICK %d %.3f\n", i, 100.0 + i * 0.1);
            write_all(fd, msg, static_cast<std::size_t>(len));
            ::usleep(2000);
        }
        ::close(fd); // EOF for reader
        std::exit(0);
    }

    pid_t reader = ::fork();
    if (reader == -1) die("fork fifo reader");

    if (reader == 0) {
        // Reader: open FIFO for reading (blocks until writer connects).
        int fd = ::open(FIFO_PATH, O_RDONLY);
        if (fd == -1) die("open fifo read");
        char buf[256]{};
        ssize_t total = 0;
        while (true) {
            ssize_t n = ::read(fd, buf + total,
                               sizeof(buf) - 1 - static_cast<std::size_t>(total));
            if (n == 0) break; // writer closed
            if (n == -1) { if (errno == EINTR) continue; die("read fifo"); }
            total += n;
        }
        ::close(fd);
        buf[total] = '\0';
        std::printf("  reader: %zd bytes received:\n%s", total, buf);
        std::exit(0);
    }

    int s1, s2;
    ::waitpid(writer, &s1, 0);
    ::waitpid(reader, &s2, 0);
    ::unlink(FIFO_PATH);
}

int main()
{
    demo_anonymous_pipe();
    demo_dup2_stdout();
    demo_socketpair();
    demo_fifo();
}
