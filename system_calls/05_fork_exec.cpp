#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// fork, exec family, waitpid, pipe across fork, WNOHANG.
//
// fork()
//   Duplicates the calling process (copy-on-write).
//   Returns 0 in the child, child PID in the parent, -1 on error.
//   Both processes continue from the line AFTER fork().
//   File descriptors are inherited; use O_CLOEXEC to suppress that.
//
// exec family
//   Replaces the current process image with a new program.
//   execvp(file, argv) — searches PATH; inherits environment.
//   execve(path, argv, envp) — full control, no PATH search.
//   On success, execvp does not return.
//
// waitpid(pid, &status, options)
//   Reaps a child. Without this, a dead child becomes a zombie — it
//   holds a slot in the process table until the parent calls wait.
//   Returning -1 with errno == ECHILD means no more children.
//
// Status macros:
//   WIFEXITED(st)   — child exited normally via exit() or return
//   WEXITSTATUS(st) — the exit code (0–255)
//   WIFSIGNALED(st) — child was killed by a signal
//   WTERMSIG(st)    — which signal killed it
//   WIFSTOPPED(st)  — child was stopped (SIGSTOP / SIGTSTP)
//
// WNOHANG: return 0 immediately if no child has exited yet.
//   Used to poll children in a non-blocking event loop.
//
// Pipe across fork:
//   parent creates pipe → forks → child inherits both ends.
//   Convention: parent writes, child reads (or vice versa).
//   The unused end MUST be closed in each process; otherwise the
//   reader never sees EOF.
//
// Compile:  g++ -std=c++20 05_fork_exec.cpp -o fork_exec && ./fork_exec
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

static void print_status(pid_t pid, int status)
{
    if (WIFEXITED(status))
        std::printf("  pid %d exited with code %d\n",
                    (int)pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        std::printf("  pid %d killed by signal %d\n",
                    (int)pid, WTERMSIG(status));
}

// ── PART 1: basic fork ────────────────────────────────────────────────
static void demo_basic_fork()
{
    std::puts("── basic fork ───────────────────────────────────");
    pid_t child = ::fork();
    if (child == -1) die("fork");

    if (child == 0) {
        std::printf("  child: PID=%d  parent=%d\n",
                    (int)::getpid(), (int)::getppid());
        std::exit(42);
    }

    int status;
    pid_t reaped = ::waitpid(child, &status, 0);
    print_status(reaped, status);
    std::puts("  parent: child reaped (no zombie)");
}

// ── PART 2: execvp — replace child image ─────────────────────────────
static void demo_execvp()
{
    std::puts("\n── execvp: run `uname -s` in child ──────────────");
    pid_t child = ::fork();
    if (child == -1) die("fork execvp");

    if (child == 0) {
        char* argv[] = { const_cast<char*>("uname"),
                         const_cast<char*>("-s"),
                         nullptr };
        ::execvp("uname", argv);
        die("execvp"); // reached only if exec fails
    }

    int status;
    pid_t r = ::waitpid(child, &status, 0);
    print_status(r, status);
}

// ── PART 3: pipe across fork ──────────────────────────────────────────
// Parent: writes order IDs.  Child: reads and prints them.
static void demo_pipe_fork()
{
    std::puts("\n── pipe across fork ─────────────────────────────");
    int pipefd[2];
    if (::pipe(pipefd) == -1) die("pipe");
    // pipefd[0] = read end, pipefd[1] = write end.

    pid_t child = ::fork();
    if (child == -1) die("fork pipe");

    if (child == 0) {
        // Child is the reader — close unused write end.
        ::close(pipefd[1]);

        char buf[128]{};
        ssize_t total = 0;
        while (true) {
            ssize_t n = ::read(pipefd[0], buf + total,
                               sizeof(buf) - 1 - static_cast<std::size_t>(total));
            if (n <= 0) break; // EOF when parent closes write end
            total += n;
        }
        ::close(pipefd[0]);
        std::printf("  child received: %s", buf);
        std::exit(0);
    }

    // Parent is the writer — close unused read end.
    ::close(pipefd[0]);
    for (int id = 1001; id <= 1005; ++id) {
        char msg[32];
        int  len = std::snprintf(msg, sizeof(msg), "ORDER_%d\n", id);
        ::write(pipefd[1], msg, static_cast<std::size_t>(len));
    }
    ::close(pipefd[1]); // closing write end sends EOF to child

    int status;
    ::waitpid(child, &status, 0);
    print_status(child, status);
}

// ── PART 4: WNOHANG — non-blocking poll ──────────────────────────────
static void demo_wnohang()
{
    std::puts("\n── WNOHANG (non-blocking poll) ──────────────────");
    pid_t child = ::fork();
    if (child == -1) die("fork wnohang");

    if (child == 0) {
        ::usleep(30000); // child sleeps 30 ms
        std::exit(7);
    }

    // Parent polls without blocking.
    for (int attempt = 0; attempt < 10; ++attempt) {
        int status;
        pid_t r = ::waitpid(child, &status, WNOHANG);
        if (r == 0) {
            std::printf("  attempt %d: child not done yet\n", attempt + 1);
            ::usleep(5000); // 5 ms
        } else {
            print_status(child, status);
            break;
        }
    }
}

// ── PART 5: multiple children ────────────────────────────────────────
static void demo_multiple_children()
{
    std::puts("\n── multiple children ────────────────────────────");
    pid_t children[3];
    for (int i = 0; i < 3; ++i) {
        children[i] = ::fork();
        if (children[i] == -1) die("fork multi");
        if (children[i] == 0) {
            std::exit(i + 10); // each child exits with a distinct code
        }
    }
    // Reap in any order using waitpid(-1, ...).
    for (int i = 0; i < 3; ++i) {
        int status;
        pid_t r = ::waitpid(-1, &status, 0);
        print_status(r, status);
    }
}

int main()
{
    demo_basic_fork();
    demo_execvp();
    demo_pipe_fork();
    demo_wnohang();
    demo_multiple_children();
}
