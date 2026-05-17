#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────
// Filesystem metadata, hard links, symbolic links, rename, unlink.
//
// stat(path, &sb)   — fill struct stat; follows symlinks.
// fstat(fd, &sb)    — same from an open fd; avoids TOCTOU race.
// lstat(path, &sb)  — does NOT follow symlinks; sees the symlink itself.
//
// Key struct stat fields:
//   st_mode   — type + permissions; use S_ISREG / S_ISDIR / S_ISLNK
//   st_size   — file size in bytes
//   st_nlink  — hard link count
//   st_ino    — inode number (unique within a device)
//   st_mtime  — modification time; use to detect new market data files
//
// Hard link  — second directory entry pointing to the SAME inode.
//   Removing either name leaves the data alive.
//   Cannot cross filesystems; cannot link directories.
//
// Symbolic link — small file whose content is a path string.
//   Can cross filesystems; can dangle if the target is removed.
//
// rename(old, new) is ATOMIC on POSIX if both paths are on the same
//   filesystem. The canonical way to publish updated pricing curves or
//   configuration files without readers ever seeing partial data.
//
// unlink(path) — removes a directory entry.
//   The inode is freed only when ALL hard links are gone AND no process
//   has the file open. Allows "open then unlink" for temp-file safety.
//
// Compile:  g++ -std=c++20 02_stat_links.cpp -o stat_links && ./stat_links
// ─────────────────────────────────────────────────────────────────────

static void die(const char* msg) { std::perror(msg); std::exit(1); }

static void print_stat(const char* label, const char* path)
{
    struct stat sb;
    if (::lstat(path, &sb) == -1) { std::perror(path); return; }

    const char* type = S_ISREG(sb.st_mode)  ? "reg "
                     : S_ISDIR(sb.st_mode)  ? "dir "
                     : S_ISLNK(sb.st_mode)  ? "link"
                     :                        "??? ";

    // Build "rwxr-xr-x" string from mode bits.
    char p[10];
    std::snprintf(p, sizeof(p), "%c%c%c%c%c%c%c%c%c",
        (sb.st_mode & S_IRUSR) ? 'r' : '-', (sb.st_mode & S_IWUSR) ? 'w' : '-',
        (sb.st_mode & S_IXUSR) ? 'x' : '-', (sb.st_mode & S_IRGRP) ? 'r' : '-',
        (sb.st_mode & S_IWGRP) ? 'w' : '-', (sb.st_mode & S_IXGRP) ? 'x' : '-',
        (sb.st_mode & S_IROTH) ? 'r' : '-', (sb.st_mode & S_IWOTH) ? 'w' : '-',
        (sb.st_mode & S_IXOTH) ? 'x' : '-');

    char mtime[20];
    std::strftime(mtime, sizeof(mtime), "%H:%M:%S",
                  std::localtime(&sb.st_mtime));

    std::printf("  %-16s %s %s  size=%-6lld  nlink=%ld  ino=%llu\n",
        label, type, p, (long long)sb.st_size,
        (long)sb.st_nlink, (unsigned long long)sb.st_ino);
}

int main()
{
    const char* file  = "/tmp/curve.dat";
    const char* hard  = "/tmp/curve.hard";
    const char* sym   = "/tmp/curve.sym";
    const char* tmp   = "/tmp/curve.new";
    const char* final = "/tmp/curve.final";

    // ── Create a file ─────────────────────────────────────────────────
    int fd = ::open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) die("open");
    ::write(fd, "0.045\n0.047\n0.051\n", 18);
    ::close(fd);

    std::cout << "── Initial stat ─────────────────────────────────\n";
    print_stat(file, file);

    // ── Hard link: second name for the same inode ─────────────────────
    if (::link(file, hard) == -1) die("link");
    std::cout << "\n── After link() ─────────────────────────────────\n";
    print_stat(file, file);   // nlink is now 2
    print_stat(hard, hard);   // same inode as file

    // ── Symbolic link: indirect reference by path string ──────────────
    if (::symlink(file, sym) == -1) die("symlink");
    std::cout << "\n── After symlink() ──────────────────────────────\n";
    print_stat(sym,  sym);    // lstat: shows as "link"; size = len of target path
    // stat (follows the link) sees the target:
    struct stat followed;
    ::stat(sym, &followed);
    std::printf("  stat(sym) size (target file) = %lld\n",
                (long long)followed.st_size);

    // ── readlink: where does the symlink point? ───────────────────────
    char target[256]{};
    ssize_t n = ::readlink(sym, target, sizeof(target) - 1);
    if (n != -1)
        std::printf("\n  readlink: %s → %s\n", sym, target);

    // ── fstat: get metadata from an open fd (avoids TOCTOU race) ──────
    std::cout << "\n── fstat from open fd ───────────────────────────\n";
    int fd2 = ::open(file, O_RDONLY);
    if (fd2 == -1) die("open fstat");
    struct stat fsb;
    ::fstat(fd2, &fsb);
    ::close(fd2);
    std::printf("  size=%lld  ino=%llu  nlink=%ld\n",
        (long long)fsb.st_size,
        (unsigned long long)fsb.st_ino, (long)fsb.st_nlink);

    // ── rename: atomic file replacement ──────────────────────────────
    // Pattern: write new content to a temp file → rename → atomic swap.
    // Readers opening the final path will always see a complete file.
    std::cout << "\n── rename (atomic replacement) ──────────────────\n";
    int tfd = ::open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd == -1) die("open tmp");
    ::write(tfd, "new_curve_data", 14);
    ::close(tfd);

    if (::rename(tmp, final) == -1) die("rename");
    std::cout << "  " << tmp << " → " << final << " (atomic)\n";
    print_stat(final, final);

    // ── unlink: remove a directory entry ─────────────────────────────
    std::cout << "\n── unlink ───────────────────────────────────────\n";
    ::unlink(sym);
    ::unlink(hard);           // nlink goes from 2 → 1
    print_stat(file, file);   // still alive; one link remains
    ::unlink(file);           // nlink → 0; inode freed
    ::unlink(final);

    // ── open then unlink: self-destructing temp file ───────────────────
    // File is unlinked immediately after open, but the data persists
    // as long as the fd is open. Automatically cleaned up on process exit.
    std::cout << "\n── open + immediate unlink (temp file pattern) ──\n";
    int tmp_fd = ::open("/tmp/scratch.bin", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (tmp_fd == -1) die("open scratch");
    ::unlink("/tmp/scratch.bin"); // name is gone; fd still works
    ::write(tmp_fd, "ephemeral", 9);
    ::lseek(tmp_fd, 0, SEEK_SET);
    char scratch[16]{};
    ::read(tmp_fd, scratch, sizeof(scratch) - 1);
    std::printf("  read from unlinked fd: %s\n", scratch);
    ::close(tmp_fd); // inode freed here
}
