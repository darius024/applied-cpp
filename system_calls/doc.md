# System Calls in C++

A system call is a request from user-space code to the OS kernel to perform
a privileged operation — accessing hardware, managing memory, creating processes.
The CPU switches from user mode (ring 3) to kernel mode (ring 0), executes the
kernel code, then returns. Each syscall costs ~100–300 ns on modern hardware.
Minimising syscalls is a core performance concern in low-latency systems.

---

## File descriptors

A file descriptor (fd) is a small non-negative integer — the kernel's handle to
an open file, pipe, socket, device, or timer. Three are always open at program
start:

| fd | Name   | Default destination |
|----|--------|---------------------|
| 0  | stdin  | terminal (keyboard) |
| 1  | stdout | terminal (screen)   |
| 2  | stderr | terminal (screen)   |

The kernel maintains an open-file table per process. `dup2(old, new)` makes `new`
a copy of `old` — the canonical way to redirect stdin/stdout before `exec`.
`O_CLOEXEC` marks an fd to be closed automatically across `exec`.

Every syscall returns `-1` on error and sets `errno`. **Always check return
values.** Never assume a write succeeded.

---

## File I/O

```
open(path, flags [, mode]) → fd
read(fd, buf, count)       → bytes read  (0 = EOF)
write(fd, buf, count)      → bytes written
lseek(fd, offset, whence)  → new position
close(fd)                  → 0 or -1
```

### `open` flags

| Flag          | Effect                                                        |
|---------------|---------------------------------------------------------------|
| `O_RDONLY`    | Read-only access (one of these three is required)             |
| `O_WRONLY`    | Write-only access                                             |
| `O_RDWR`      | Read and write access                                         |
| `O_CREAT`     | Create file if absent (requires `mode` argument)              |
| `O_TRUNC`     | Truncate to zero on open                                      |
| `O_APPEND`    | All writes are atomic appends to end of file                  |
| `O_EXCL`      | Fail with `EEXIST` if file already exists — atomic "create-only" |
| `O_NONBLOCK`  | Non-blocking I/O — `read`/`write` returns `EAGAIN` when not ready |
| `O_CLOEXEC`   | Close this fd automatically across `exec`                     |

`O_EXCL | O_CREAT` is the race-free way to acquire a lock file.
`O_APPEND` makes each write atomic even from concurrent processes — useful for
log files and audit trails.

### `read` / `write` semantics

- May return fewer bytes than requested (short read/write). **Always loop.**
- Return `0` on `read` means EOF (pipe/socket closed or end of file).
- Return `-1` with `errno == EINTR` means interrupted by a signal — retry.

---

## Filesystem metadata

```
stat(path, &sb)   — fill struct stat for the named path
fstat(fd, &sb)    — same, from an open fd (avoids TOCTOU race)
lstat(path, &sb)  — like stat, but does NOT follow symlinks
```

Key `struct stat` fields:

| Field       | Meaning                                    |
|-------------|--------------------------------------------|
| `st_mode`   | Type + permission bits; use `S_ISREG`, `S_ISDIR`, `S_ISLNK` |
| `st_size`   | File size in bytes                         |
| `st_nlink`  | Hard link count                            |
| `st_ino`    | Inode number (unique within a device)      |
| `st_mtime`  | Last modification time                     |

### Hard links vs symbolic links

A **hard link** is a second directory entry pointing to the same inode.
Both names are equal — deleting one leaves the file alive.
Constraints: same filesystem only; cannot link directories.

A **symlink** is a small file whose content is a path string.
Can cross filesystems; can go dangling if the target is removed.
`lstat` sees the symlink itself; `stat` follows it.

### Atomic file replacement

```cpp
// Write new data to a temp file, then atomically rename into place.
// Readers never see a partial write.
write_to("/tmp/curve.tmp");
rename("/tmp/curve.tmp", "/prod/curve.dat"); // atomic on POSIX
```

---

## Memory-mapped files (`mmap`)

```
mmap(addr, len, prot, flags, fd, offset) → void* or MAP_FAILED
munmap(addr, len)
msync(addr, len, MS_SYNC | MS_ASYNC)
madvise(addr, len, advice)
```

### Protection flags (`prot`)

`PROT_READ`, `PROT_WRITE`, `PROT_NONE` — control read/write/no-access.

### Mapping flags

| Flag           | Effect                                                          |
|----------------|-----------------------------------------------------------------|
| `MAP_PRIVATE`  | Writes go to a CoW copy; never reach the file                   |
| `MAP_SHARED`   | Writes visible to other mappings and flushed to the file        |
| `MAP_ANONYMOUS`| Not backed by a file (`fd = -1`); returns zeroed memory         |

`MAP_ANONYMOUS | MAP_SHARED` + `fork()` = cheap shared memory between parent/child.

### `madvise` hints

| Hint              | When to use                                        |
|-------------------|----------------------------------------------------|
| `MADV_SEQUENTIAL` | Scanning a tick file from start to end — prefetch  |
| `MADV_RANDOM`     | Random lookups in an order book snapshot           |
| `MADV_DONTNEED`   | After processing — OS may evict pages, saving RSS  |

### Why `mmap` over `read()` for large market data files

- **Zero-copy**: the kernel maps file pages directly into your address space.
- **Random access**: O(1) pointer arithmetic; no `lseek + read` pairs.
- **Shared pages**: multiple processes mapping the same file share physical RAM.
- The OS handles prefetching and caching automatically.

File must be large enough before mapping — use `ftruncate(fd, size)` first.
Always `munmap` when done; the fd may be closed immediately after `mmap`.

---

## Shared memory IPC

| Mechanism                        | Scope                | Cleanup           |
|----------------------------------|----------------------|-------------------|
| `MAP_ANONYMOUS | MAP_SHARED`     | Parent + children  | Freed with `munmap`|
| `shm_open` + `mmap`              | Any process by name  | `shm_unlink`      |

```cpp
// Named shared memory — writer
int fd = shm_open("/prices", O_CREAT | O_RDWR, 0600);
ftruncate(fd, sizeof(PriceTable));
void* mem = mmap(nullptr, sizeof(PriceTable),
                 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Any other process that knows "/prices" can open and map it.
```

Shared memory provides **no implicit locking**. Use:
- `std::atomic` for single-value updates (lock-free).
- A `pthread_mutex` with `PTHREAD_PROCESS_SHARED` for multi-field updates.

---

## Process management

```
fork()                     → 0 in child, child PID in parent, -1 on error
execve(path, argv, envp)   → does not return on success
execvp(file, argv)         → searches PATH; inherits environment
waitpid(pid, &status, 0)   → reap child; required to avoid zombies
```

`fork` clones the process copy-on-write — cheap until either side writes.
File descriptors are duplicated across `fork`. Use `O_CLOEXEC` to prevent
unintended fd inheritance into `exec`'d children.

### Status inspection after `waitpid`

```cpp
if (WIFEXITED(status))   printf("exit code: %d\n", WEXITSTATUS(status));
if (WIFSIGNALED(status)) printf("killed by signal: %d\n", WTERMSIG(status));
```

`WNOHANG` makes `waitpid` non-blocking — returns 0 if the child hasn't exited.

---

## Signals

```
sigaction(signum, &sa, &old_sa)  — install a handler (preferred over signal())
sigprocmask(SIG_BLOCK, &set, &old_set)  — block signals temporarily
raise(signum)                    — send signal to self
kill(pid, signum)                — send to another process
```

### `struct sigaction` flags

| Flag          | Effect                                              |
|---------------|-----------------------------------------------------|
| `SA_RESTART`  | Restart interrupted syscalls (read, write, accept)  |
| `SA_SIGINFO`  | Use `sa_sigaction` handler with extra `siginfo_t`   |
| `SA_RESETHAND`| Reset to `SIG_DFL` after first delivery             |

### Async-signal safety

A signal handler may be called at any point. Only **async-signal-safe** functions
may be called from it. Safe: `write()`, `_exit()`, atomic store.
**Unsafe**: `printf`, `malloc`, `throw`, `std::cout`, any mutex.

The canonical shutdown pattern:
```cpp
static std::atomic<bool> g_stop{false};
void handle_sigterm(int) { g_stop.store(true, std::memory_order_relaxed); }
// main loop: while (!g_stop.load()) { /* work */ }
```

### Common signals in trading systems

| Signal    | Default action | Common use                          |
|-----------|----------------|-------------------------------------|
| `SIGTERM` | terminate      | Graceful shutdown from a scheduler  |
| `SIGINT`  | terminate      | Ctrl-C from the terminal            |
| `SIGUSR1` | terminate      | Reload config / re-read curves      |
| `SIGPIPE` | terminate      | Write to a closed socket — ignore it|

---

## Pipes and FIFOs

```
pipe(pipefd[2])               — anonymous pipe; kernel buffer ~64 KB
pipe2(pipefd, O_CLOEXEC)      — same with flags
mkfifo(path, mode)            — named pipe (FIFO), any process can open it
socketpair(AF_UNIX, SOCK_STREAM, 0, sv[2]) — bidirectional pipe
```

`pipefd[0]` = read end, `pipefd[1]` = write end.
Read blocks when the pipe is empty; write blocks when the buffer is full.
Reading returns 0 (EOF) when all write ends are closed.

`dup2(pipefd[1], 1)` redirects stdout into the pipe — the standard way
to capture output from a child process.

Named FIFOs block on `open()` until both ends are connected. Use `O_NONBLOCK`
to avoid blocking in one-sided setups.

---

## I/O multiplexing

Read from N fds simultaneously without blocking on any one:

| Mechanism | Portable | Complexity | Notes                            |
|-----------|----------|------------|----------------------------------|
| `select`  | ✓        | O(max_fd)  | Limited to 1024 fds. Legacy.     |
| `poll`    | ✓        | O(n)       | Better API; recommended starting point |
| `epoll`   | Linux    | O(1)       | Scalable; edge/level-triggered   |
| `kqueue`  | macOS/BSD| O(1)       | More powerful; timer/signal filters |

```cpp
struct pollfd fds[2];
fds[0] = {fd1, POLLIN, 0};
fds[1] = {fd2, POLLIN, 0};
int ready = poll(fds, 2, 500 /*ms*/);
if (fds[0].revents & POLLIN) { /* read from fd1 */ }
if (fds[1].revents & POLLHUP) { /* fd2 was closed */ }
```

`epoll` maintains a kernel-side interest set updated with `epoll_ctl`.
`epoll_wait` returns only the ready events — efficient at thousands of fds.
Edge-triggered (`EPOLLET`) fires only on new data; must drain until `EAGAIN`.

---

## File locking

Advisory locks — only honoured by cooperating processes.

```
flock(fd, LOCK_EX | LOCK_NB)       — whole-file, BSD-style
fcntl(fd, F_SETLKW, &flock_struct) — POSIX byte-range lock
```

| Operation   | `flock` flag    | `fcntl` `l_type` |
|-------------|-----------------|-------------------|
| Read lock   | `LOCK_SH`       | `F_RDLCK`         |
| Write lock  | `LOCK_EX`       | `F_WRLCK`         |
| Unlock      | `LOCK_UN`       | `F_UNLCK`         |
| Non-blocking| `LOCK_NB`       | `F_SETLK`         |

**PID lock file**: create `/var/run/app.pid` with `O_CREAT | O_EXCL`, write PID.
The OS releases the lock when the process dies — crash-safe mutual exclusion.

---

## Examples

| File                     | Covers                                                          |
|--------------------------|-----------------------------------------------------------------|
| `01_file_io.cpp`         | `open`, `read`, `write`, `lseek`, `dup2`, `O_APPEND`, `O_EXCL` |
| `02_stat_links.cpp`      | `stat`/`fstat`/`lstat`, `link`, `symlink`, `readlink`, `rename`, `unlink` |
| `03_mmap_file.cpp`       | `mmap` for reading a tick file, `madvise`, `msync`, `munmap`   |
| `04_mmap_ipc.cpp`        | Anonymous shared mapping, `shm_open`/`shm_unlink`, atomics     |
| `05_fork_exec.cpp`       | `fork`, `execvp`, `waitpid`, pipe across fork, `WNOHANG`       |
| `06_signals.cpp`         | `sigaction`, graceful shutdown, `sigprocmask`, `SIGUSR1`        |
| `07_pipes_fifo.cpp`      | `pipe`, `dup2`, `socketpair`, `mkfifo`                         |
| `08_poll.cpp`            | `poll` multiplexing multiple market data feeds                 |
| `09_file_locking.cpp`    | `flock`, `fcntl` byte-range, PID lock file                     |
| `10_epoll_kqueue.cpp`    | `epoll` (Linux) / `kqueue` (macOS) scalable event loop         |
