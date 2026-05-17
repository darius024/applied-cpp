#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// Toolchain: ccache, linker selection, linker flags, cross-compilation.
//
// Compile:
//   g++ -std=c++20 -O2 10_toolchain.cpp -o toolchain && ./toolchain
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── PART 1: ccache ────────────────────────────────────────────────────
//
// ccache is a compiler cache.  It wraps the compiler invocation:
//   ccache g++ -std=c++20 -O2 main.cpp -o main.o
//
// On a cache hit, it returns the cached .o file without invoking the
// compiler — typically in < 5 ms regardless of TU size.
// On a cache miss, it compiles normally and stores the result.
//
// Cache key: hash of (preprocessed source + compiler flags + compiler binary).
// Cache location: ~/.cache/ccache  (configurable)
//
// Integration:
//   cmake -B build -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
//   # or globally:
//   export CXX="ccache g++"
//
// Configuration (~/.config/ccache/ccache.conf):
//   max_size = 20G
//   compression = true
//   sloppiness = pch_defines,time_macros
//   # sloppiness: allow caching even if __TIME__ differs (PCH builds)
//
// Typical metrics (warm CI cache):
//   Cache hit rate: 95%+
//   Hit cost:       < 5 ms (vs 50-500 ms per TU compile)
//   Miss cost:      normal compile + ~10 ms overhead to store
//
static void demo_ccache()
{
    section("ccache");
    std::puts("  ccache wraps compiler calls and caches object files by content hash.");
    std::puts("");
    std::puts("  Install:  brew install ccache  /  apt install ccache");
    std::puts("  CMake:    cmake -B build -DCMAKE_CXX_COMPILER_LAUNCHER=ccache");
    std::puts("  Stats:    ccache --show-stats");
    std::puts("  Zero:     ccache --zero-stats");
    std::puts("  Clear:    ccache --clear");
    std::puts("");
    std::puts("  ~/.config/ccache/ccache.conf:");
    std::puts("    max_size = 20G");
    std::puts("    compression = true");
    std::puts("    sloppiness = pch_defines,time_macros");
}

// ── PART 2: linker selection ──────────────────────────────────────────
//
// The linker is the dominant step for large C++ projects (often > 50%
// of clean build time).  Switching from ld.bfd to lld or mold can
// reduce link time from 30 s → 3 s on large codebases.
//
// Benchmarks (a 500K-line C++ project, approximate):
//   ld.bfd:  30 s
//   ld.gold: 18 s
//   lld:      6 s
//   mold:     2 s
//
// How to select:
//   g++ -fuse-ld=lld main.o -o app
//   cmake -B build -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
//
static void demo_linker()
{
    section("linker selection (ld → gold → lld → mold)");
    std::puts("  ld.bfd  (default GNU): -fuse-ld=bfd   — slowest, most compatible");
    std::puts("  ld.gold             : -fuse-ld=gold  — better C++, 2× faster");
    std::puts("  lld     (LLVM)      : -fuse-ld=lld   — 5× faster, ThinLTO support");
    std::puts("  mold                : -fuse-ld=mold  — fastest; parallel; macOS dyld support");
    std::puts("");
    std::puts("  Install (Linux):");
    std::puts("    apt install lld mold");
    std::puts("  macOS:");
    std::puts("    brew install llvm mold");
    std::puts("  CMake:");
    std::puts("    cmake -B build -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld");
}

// ── PART 3: dead code elimination ─────────────────────────────────────
//
// By default, unused functions and globals remain in the final binary.
// -ffunction-sections + -fdata-sections puts each symbol in its own section.
// --gc-sections (Linux) / -dead_strip (macOS) then removes unreferenced ones.
//
// Typical binary size reduction: 10–40% on large projects.
//
static void demo_dead_strip()
{
    section("dead code / section elimination");
    std::puts("  Normal build: all object code retained even if unreachable.");
    std::puts("");
    std::puts("  Linux:");
    std::puts("    g++ -ffunction-sections -fdata-sections -O2 main.cpp \\");
    std::puts("        -Wl,--gc-sections -o app");
    std::puts("");
    std::puts("  macOS:");
    std::puts("    g++ -O2 main.cpp -Wl,-dead_strip -o app");
    std::puts("");
    std::puts("  Also useful:");
    std::puts("    -Wl,--as-needed   — skip linking unused shared libs");
    std::puts("    -Wl,--strip-debug — strip debug info from final binary");
    std::puts("    objcopy --only-keep-debug app app.dbg  — external debug");
}

// ── PART 4: cross-compilation ─────────────────────────────────────────
//
// Build for a different target architecture/OS.
// Requires: a cross-compilation toolchain + target sysroot.
//
// Common setups:
//   x86_64 → aarch64 Linux:  aarch64-linux-gnu-g++
//   x86_64 → RISC-V:         riscv64-linux-gnu-g++
//   macOS  → Linux (in CI):  use a Docker container with a cross-toolchain
//
// CMake toolchain file (toolchain.cmake):
//   set(CMAKE_SYSTEM_NAME   Linux)
//   set(CMAKE_SYSTEM_PROCESSOR aarch64)
//   set(CMAKE_C_COMPILER    aarch64-linux-gnu-gcc)
//   set(CMAKE_CXX_COMPILER  aarch64-linux-gnu-g++)
//   set(CMAKE_SYSROOT       /opt/sysroots/aarch64)
//   set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
//   set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
//   set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
//
// Invoke:
//   cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
//   cmake --build build
//   # Run on target or with QEMU user emulation:
//   qemu-aarch64 -L /opt/sysroots/aarch64 ./app
//
static void demo_cross_compile()
{
    section("cross-compilation");
    std::printf("  Build host:   %s\n",
#if defined(__aarch64__)
        "aarch64"
#elif defined(__x86_64__)
        "x86_64"
#elif defined(__riscv)
        "RISC-V"
#else
        "unknown"
#endif
    );

    std::puts("  Cross-compile via CMake toolchain file:");
    std::puts("    cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake");
    std::puts("    # toolchain.cmake sets CMAKE_C/CXX_COMPILER and CMAKE_SYSROOT");
    std::puts("");
    std::puts("  Vcpkg toolchain (for dependency management + cross-compile):");
    std::puts("    cmake -B build \\");
    std::puts("      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \\");
    std::puts("      -DVCPKG_TARGET_TRIPLET=arm64-linux");
}

// ── PART 5: pkg-config integration ────────────────────────────────────
//
// pkg-config is the POSIX way for installed libraries to advertise their
// compiler and linker flags.  CMake's FindPkgConfig module wraps it.
//
// CMake usage:
//   find_package(PkgConfig REQUIRED)
//   pkg_check_modules(LIBSODIUM REQUIRED libsodium)
//   target_include_directories(my_app PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
//   target_link_libraries(my_app PRIVATE ${LIBSODIUM_LIBRARIES})
//
// Manual query:
//   pkg-config --cflags --libs libsodium
//   # outputs: -I/usr/include/sodium  -lsodium
//
static void demo_pkgconfig()
{
    section("pkg-config");
    std::puts("  pkg-config --cflags --libs <lib>   # query a library");
    std::puts("  CMake: find_package(PkgConfig REQUIRED)");
    std::puts("         pkg_check_modules(LIB REQUIRED libname)");
    std::puts("         target_link_libraries(app PRIVATE ${LIB_LIBRARIES})");
}

// ── PART 6: reproducible builds ───────────────────────────────────────
//
// Reproducible builds: given the same source, produce bit-identical output.
// Required for: security audits, binary transparency, cache effectiveness.
//
// Common sources of non-determinism:
//   - __DATE__ / __TIME__ macros → use SOURCE_DATE_EPOCH env var
//   - Build path embedded in debug info → -fdebug-prefix-map=OLD=NEW
//   - Linker map ordering → -Wl,--sort-section,alignment
//   - Parallel compilation race in linker → use deterministic flags
//
// Flags for reproducibility:
//   g++ -fmacro-prefix-map=$(pwd)=. \
//       -fdebug-prefix-map=$(pwd)=. \
//       -Wno-builtin-macro-redefined \
//       -D__DATE__='"reproducible"' \
//       -D__TIME__='"reproducible"'
//
static void demo_reproducible()
{
    section("reproducible builds");
    std::puts("  Sources of non-determinism:");
    std::puts("    __DATE__ / __TIME__     → -D__DATE__='\"\"' -D__TIME__='\"\"'");
    std::puts("    build paths in DWARF    → -fdebug-prefix-map=$(pwd)=.");
    std::puts("    macro paths             → -fmacro-prefix-map=$(pwd)=.");
    std::puts("    SOURCE_DATE_EPOCH       → set to a fixed Unix timestamp");
    std::puts("");
    std::puts("  Verify: build twice and diff the binaries:");
    std::puts("    diff <(sha256sum build1/app) <(sha256sum build2/app)");
}

int main()
{
    std::puts("=== toolchain: ccache, linkers, cross-compile ===");
    demo_ccache();
    demo_linker();
    demo_dead_strip();
    demo_cross_compile();
    demo_pkgconfig();
    demo_reproducible();
    std::puts("\n=== see doc.md for full reference ===");
}
