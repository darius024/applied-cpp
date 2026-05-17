#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// Signals: sigaction, async-signal safety, graceful shutdown, sigprocmask.
//
// sigaction(signum, &new_action, &old_action)
//   The preferred way to install signal handlers.  signal() is legacy
//   and has unspecified behaviour on many edge cases.
//
// struct sigaction fields:
//   sa_handler    — simple handler: SIG_DFL, SIG_IGN, or function ptr
//   sa_sigaction  — extended handler, used with SA_SIGINFO
//   sa_mask       — signals blocked while this handler runs
//   sa_flags      — modifiers:
//     SA_RESTART  — restart interrupted syscalls (read, write, accept)
//                   avoids checking EINTR in most loops
//     SA_SIGINFO  — use sa_sigaction; handler receives siginfo_t
//     SA_RESETHAND — revert to SIG_DFL after first delivery
//
// Async-signal safety:
//   A signal can be delivered at ANY point in the program.
//   Only async-signal-safe functions may be called from handlers.
//   SAFE:   write(), _exit(), atomic store/load, raise()
//   UNSAFE: printf, malloc, new, throw, std::cout, any mutex
//   Canonical shutdown pattern: set a std::atomic<bool> in the handler;
//   check it in the main loop.
//
// sigprocmask(SIG_BLOCK | SIG_UNBLOCK | SIG_SETMASK, &set, &old)
//   Block delivery of signals — e.g. during a critical section that
//   must not be interrupted even by async-safe handler code.
//
// Common signals in trading systems:
//   SIGTERM — graceful shutdown from a process manager / scheduler
//   SIGINT  — Ctrl-C from the terminal
//   SIGUSR1 — application-defined; reload config / re-read yield curves
//   SIGPIPE — write to a closed socket; must be ignored or handled
//
// Compile:  g++ -std=c++20 06_signals.cpp -o signals && ./signals
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

// ── Shared state (signal-handler safe) ───────────────────────────────
// Only std::atomic is safe to read/write from both handler and main.
static std::atomic<bool> g_stop{false};
static std::atomic<bool> g_reload{false};
static std::atomic<int>  g_last_signal{0};

// ── Signal handler — must be async-signal-safe ────────────────────────
// Writing to an atomic<bool> is safe; printf is not.
static void handle_signal(int sig)
{
    g_last_signal.store(sig, std::memory_order_relaxed);
    if (sig == SIGTERM || sig == SIGINT)
        g_stop.store(true, std::memory_order_relaxed);
    else if (sig == SIGUSR1)
        g_reload.store(true, std::memory_order_relaxed);
}

static void install_handler(int signum, bool restart_syscalls = true)
{
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    ::sigemptyset(&sa.sa_mask);
    if (restart_syscalls)
        sa.sa_flags |= SA_RESTART; // don't break read/write with EINTR
    if (::sigaction(signum, &sa, nullptr) == -1) die("sigaction");
}

// ── SIGPIPE: ignore it ────────────────────────────────────────────────
// A write to a closed pipe/socket delivers SIGPIPE.
// Default action: terminate.  In a trading app, we prefer to let the
// write() return -1 with errno == EPIPE and handle it at the call site.
static void ignore_sigpipe()
{
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGPIPE, &sa, nullptr);
}

// ── sigprocmask: temporarily block signals ────────────────────────────
// Use to protect a non-reentrant critical section from being interrupted
// by the signal handler (e.g., updating a multi-field struct).
static void demo_sigprocmask()
{
    std::puts("\n── sigprocmask: block SIGUSR1 during critical section ─");
    sigset_t block_set, old_set;
    ::sigemptyset(&block_set);
    ::sigaddset(&block_set, SIGUSR1);

    // Block SIGUSR1 delivery.
    if (::sigprocmask(SIG_BLOCK, &block_set, &old_set) == -1)
        die("sigprocmask block");

    std::puts("  SIGUSR1 is now blocked — simulating critical update...");
    ::usleep(5000); // work that must not be interrupted

    // Restore previous mask — SIGUSR1 may be delivered now.
    if (::sigprocmask(SIG_SETMASK, &old_set, nullptr) == -1)
        die("sigprocmask restore");
    std::puts("  SIGUSR1 unblocked");
}

int main()
{
    install_handler(SIGTERM);
    install_handler(SIGINT);
    install_handler(SIGUSR1);
    ignore_sigpipe();

    // ── Graceful shutdown loop ────────────────────────────────────────
    // Main loop checks the atomic flag set by the handler.
    std::puts("── graceful shutdown loop ────────────────────────");
    std::puts("  running... will self-terminate via SIGTERM");

    // Simulate: send SIGTERM to ourselves after a short pause.
    pid_t self = ::getpid();
    pid_t child = ::fork();
    if (child == -1) die("fork");

    if (child == 0) {
        ::usleep(30000); // 30 ms
        ::kill(self, SIGTERM);
        std::exit(0);
    }

    int iterations = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        // Simulate market data processing work (1 ms per iteration).
        ::usleep(1000);
        ++iterations;
    }

    int status;
    ::waitpid(child, &status, 0);
    std::printf("  stopped after %d iterations, signal=%d\n",
                iterations, g_last_signal.load());

    // ── SIGUSR1: config reload ────────────────────────────────────────
    std::puts("\n── SIGUSR1 config reload ─────────────────────────");
    g_stop.store(false, std::memory_order_relaxed);

    pid_t child2 = ::fork();
    if (child2 == -1) die("fork2");

    if (child2 == 0) {
        ::usleep(20000);
        ::kill(self, SIGUSR1);
        ::usleep(20000);
        ::kill(self, SIGTERM);
        std::exit(0);
    }

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (g_reload.exchange(false, std::memory_order_acq_rel)) {
            std::puts("  received SIGUSR1 — reloading yield curves...");
        }
        ::usleep(1000);
    }

    int status2;
    ::waitpid(child2, &status2, 0);
    std::puts("  final shutdown via SIGTERM");

    demo_sigprocmask();

    // ── SA_SIGINFO example: inspect siginfo_t ─────────────────────────
    // SA_SIGINFO gives the handler access to who sent the signal and why.
    std::puts("\n── SA_SIGINFO ────────────────────────────────────");
    struct sigaction sa_info{};
    sa_info.sa_sigaction = [](int sig, siginfo_t* info, void*) {
        // async-signal-safe: only write() is safe here.
        // We access info->si_pid and info->si_uid.
        char buf[64];
        int  n = std::snprintf(buf, sizeof(buf),
            "  SA_SIGINFO: sig=%d sender_pid=%d\n", sig, (int)info->si_pid);
        ::write(1, buf, static_cast<std::size_t>(n));
    };
    sa_info.sa_flags = SA_SIGINFO;
    ::sigemptyset(&sa_info.sa_mask);
    ::sigaction(SIGUSR2, &sa_info, nullptr);

    ::raise(SIGUSR2); // send to self
    ::usleep(5000);

    std::puts("\ndone.");
}
