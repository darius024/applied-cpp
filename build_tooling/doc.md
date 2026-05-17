# Build & Tooling

Deep reference for CMake, Ninja, sanitizers, static analysis, and the
broader C++ toolchain.  Every concept below maps to a numbered example file.

---

## 1. The Build Model

A C++ build pipeline has five distinct phases:

```
Source files (.cpp)
  └─► Preprocessing    (cpp)      — expand macros, resolve #includes
        └─► Compilation (cc1)     — parse, optimise, emit .o
              └─► Assembly (as)   — .s → .o  (usually folded into cc1)
                    └─► Linking   (ld/lld/mold) — combine .o → executable/library
                          └─► Runtime loader     — dlopen / ld.so
```

**Translation Unit (TU)** — one `.cpp` file after preprocessing.
Each TU is compiled independently; this is what enables parallel builds.

**Object file (.o/.obj)** — ELF/Mach-O/COFF binary with:
- `.text` — machine code
- `.data` / `.bss` — initialised / zero-initialised globals
- A symbol table (exported definitions + undefined references)
- Relocation records (addresses to be filled in by the linker)

**Static library (.a/.lib)** — an `ar` archive of object files.
The linker pulls in only the `.o` files that satisfy unresolved references.

**Shared library (.so/.dylib/.dll)** — a position-independent executable
loaded into the process address space at runtime.

---

## 2. CMake

CMake is a *meta-build system*: it generates build files for a back-end
(Make, Ninja, Xcode, Visual Studio, etc.).

### 2.1 Core concepts

```
CMakeLists.txt           ← project description
cmake -B build -G Ninja  ← configure phase: read CMakeLists, emit build files
cmake --build build      ← build phase: invoke the back-end
cmake --install build    ← install phase: copy artefacts to prefix
ctest --test-dir build   ← test phase: run CTest tests
cpack --config build/CPackConfig.cmake ← package phase
```

### 2.2 Targets — the modern CMake mental model

Everything in modern CMake revolves around **targets**.
A target is a named node in the build graph with:
- **Properties** — compile flags, include paths, link libraries, etc.
- **Dependencies** — `target_link_libraries` creates edges.
- **Usage requirements** — properties that propagate to consumers.

```cmake
add_executable(my_app main.cpp)
add_library(my_lib STATIC lib.cpp)

# PRIVATE  → only for this target's own compilation/linking
# INTERFACE → only propagated to consumers (not used by target itself)
# PUBLIC    → both PRIVATE + INTERFACE

target_include_directories(my_lib
    PUBLIC  include/    # consumers get this include path too
    PRIVATE src/        # only my_lib sees this
)

target_compile_options(my_lib PRIVATE -Wall -Wextra)
target_compile_features(my_lib PUBLIC cxx_std_20)
target_link_libraries(my_app PRIVATE my_lib)
```

### 2.3 Build types

```cmake
cmake -B build -DCMAKE_BUILD_TYPE=Debug          # -g, no optimisation
cmake -B build -DCMAKE_BUILD_TYPE=Release         # -O3 -DNDEBUG
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo  # -O2 -g -DNDEBUG
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel      # -Os -DNDEBUG
```

For multi-config generators (Xcode, VS): omit `CMAKE_BUILD_TYPE` and use:
```
cmake --build build --config Release
```

### 2.4 find_package

CMake ships *Find modules* and *Config files* for popular libraries.
Config-mode (preferred): the library installs `FooConfig.cmake` into
its install tree.

```cmake
find_package(Threads REQUIRED)
find_package(Boost 1.83 REQUIRED COMPONENTS system filesystem)

target_link_libraries(my_app PRIVATE
    Threads::Threads
    Boost::system
    Boost::filesystem
)
```

Difference from raw `-lboost_system`:
- Imported targets carry their include dirs, compile defs, and transitive
  dependencies automatically.

### 2.5 FetchContent (C++17 CMake ≥ 3.14)

Download and integrate a dependency at configure time, no pre-install needed.

```cmake
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)   # makes gtest::gtest target available
target_link_libraries(my_test PRIVATE GTest::gtest_main)
```

### 2.6 Generator expressions

Evaluated lazily during the generate phase, not the configure phase.
Common forms:

```cmake
$<CONFIG:Release>              # 1 if current config is Release, else 0
$<TARGET_FILE:tgt>             # full path to target output file
$<INSTALL_INTERFACE:include>   # only when installed
$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
$<$<CONFIG:Debug>:-O0 -g3>    # conditional flag
```

### 2.7 CMakePresets.json (CMake ≥ 3.19)

Replaces long cmake invocation scripts.  Checked into source control.

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "ENABLE_ASAN": "ON"
      }
    },
    {
      "name": "release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug"   },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

Usage: `cmake --preset debug && cmake --build --preset debug`

### 2.8 Custom commands and targets

```cmake
# Run a code generator as part of the build
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/gen/schema.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/gen.py
                -i ${CMAKE_SOURCE_DIR}/schema.json
                -o ${CMAKE_BINARY_DIR}/gen/schema.h
    DEPENDS ${CMAKE_SOURCE_DIR}/schema.json
)

add_custom_target(generate_schema
    DEPENDS ${CMAKE_BINARY_DIR}/gen/schema.h
)
add_dependencies(my_app generate_schema)

# Always-run target (e.g., linting)
add_custom_target(lint
    COMMAND clang-tidy -p ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/*.cpp
    COMMENT "Running clang-tidy"
)
```

### 2.9 install() and CPack

```cmake
install(TARGETS my_lib my_app
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)
install(DIRECTORY include/ DESTINATION include)

# Generate a .tar.gz / .deb / .rpm:
include(CPack)
set(CPACK_PACKAGE_NAME "MyLib")
set(CPACK_PACKAGE_VERSION "1.2.3")
```

---

## 3. Ninja

Ninja is a small, fast build system designed to have its input files
generated by a higher-level tool (CMake, Meson, GN, etc.).

### 3.1 Key design decisions

| Property | Make | Ninja |
|---|---|---|
| Language | Turing-complete Makefile DSL | Minimal, no control flow |
| Dependency tracking | Manual / `gcc -M` | Always generated by meta-tool |
| Build graph | Re-read every run | Memory-mapped; incremental |
| Parallelism | `-j N` | Auto-detected (`-j` defaults to CPU count × 2) |
| Startup overhead | ~50 ms | ~5 ms |

### 3.2 Useful Ninja commands

```bash
cmake -B build -G Ninja
cmake --build build -- -j8          # pass -j8 to ninja
ninja -C build                      # run ninja directly
ninja -C build -n                   # dry run (print commands only)
ninja -C build -d stats             # print build statistics
ninja -C build -t clean             # remove all outputs
ninja -C build -t compdb > compile_commands.json   # emit compilation DB
ninja -C build -t targets all       # list all known targets
ninja -C build my_app               # build a specific target
ninja -C build -v                   # verbose: show full commands
```

### 3.3 build.ninja format (for understanding)

```ninja
rule CXX
  command = g++ -std=c++20 $CXXFLAGS -c $in -o $out
  description = Compiling $in

build main.o: CXX main.cpp
  CXXFLAGS = -O2 -Wall

build myapp: LINK main.o lib.o
  LDFLAGS = -lpthread
```

---

## 4. Compiler Flags

### 4.1 Warning flags

```bash
-Wall           # widely-agreed useful warnings
-Wextra         # extra checks (unused params, sign comparison, etc.)
-Wpedantic      # strict ISO C++ conformance
-Werror         # turn all warnings into errors (use in CI, not dev)
-Wshadow        # local variable shadows outer variable
-Wconversion    # implicit type conversions that may lose data
-Wsign-conversion
-Wnull-dereference
-Wold-style-cast  # C-style (int*) casts
-Wformat=2        # printf format string vulnerabilities
-Wimplicit-fallthrough
-Wundef           # undefined macro used in #if
```

### 4.2 Optimisation flags

| Flag | Effect |
|------|--------|
| `-O0` | No optimisation; fastest compile, best debug |
| `-O1` | Basic opts: inlining of small functions, dead code elim |
| `-O2` | Standard production; vectorisation, CSE, loop transforms |
| `-O3` | Aggressive: more inlining, loop unrolling |
| `-Os` | Size optimisation (subset of -O2) |
| `-Oz` | Clang: even smaller than -Os |
| `-Og` | GCC: optimise for debugging experience |
| `-Ofast` | -O3 + `-ffast-math` (breaks IEEE 754 — use only in hot paths) |
| `-ffast-math` | Allow FP re-association, assume no NaN/Inf |
| `-march=native` | Use all CPU features of the build machine (not portable) |
| `-mtune=native` | Tune for local CPU but stay ABI-compatible |

### 4.3 Debug flags

```bash
-g          # DWARF debug info (level 2 by default)
-g3         # full debug info including macro definitions
-ggdb       # GDB-specific extra info
-gsplit-dwarf  # split DWARF into .dwo (faster link, smaller main binary)
-fdebug-prefix-map=OLD=NEW  # rewrite source paths in DWARF
```

### 4.4 Security hardening flags

```bash
-D_FORTIFY_SOURCE=2   # check buffer sizes at runtime (glibc)
-fstack-protector-strong  # stack canary on functions with buffers
-fstack-clash-protection  # detect stack/heap collision
-fcf-protection=full  # Intel CET (control-flow enforcement)
-Wl,-z,relro,-z,now   # read-only GOT after startup (Linux ELF)
-fpie -pie            # position-independent executable
```

---

## 5. Sanitizers

All sanitizers work by inserting compiler instrumentation that checks
invariants at runtime.  They have no effect on uninstrumented code
(e.g., system libraries not compiled with the sanitizer).

### 5.1 AddressSanitizer (ASan)

```bash
-fsanitize=address -fno-omit-frame-pointer -g
```

Detects:
- Heap buffer overflow / underflow
- Stack buffer overflow
- Global buffer overflow
- Use-after-free
- Use-after-return
- Use-after-scope
- Double-free / invalid free
- Memory leaks (on Linux; requires `-fsanitize=leak` or `detect_leaks=1`)

Mechanism: redzones (poisoned memory) around every allocation; shadow
memory maps 1:8 to the full address space (1 shadow byte per 8 app bytes).

Overhead: ~2× memory, ~2× runtime speed.  Not suitable for production.

Runtime options (via `ASAN_OPTIONS` env var):
```bash
ASAN_OPTIONS=halt_on_error=0:detect_leaks=1:verbosity=1 ./app
```

Key options: `detect_leaks`, `halt_on_error`, `abort_on_error`,
`log_path`, `check_initialization_order`, `detect_stack_use_after_return`.

Symboliser — get human-readable stack traces:
```bash
ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer) ./app
# or: export ASAN_OPTIONS=symbolize=1
```

### 5.2 ThreadSanitizer (TSan)

```bash
-fsanitize=thread -g
```

Detects:
- Data races (unsynchronised read/write from different threads)
- Lock-order inversions (potential deadlocks)
- Thread leaks

Mechanism: shadow memory per memory location recording last 4 accesses
(vector clock algorithm — happens-before tracking).

Overhead: ~5–15× memory, ~5–15× runtime.

Notes:
- **Incompatible with ASan** — cannot combine `-fsanitize=address,thread`.
- Requires recompilation of all libraries for accurate results.
- False positives possible with lock-free code using `relaxed` ordering —
  TSan models C++ memory model but some patterns confuse it.

Runtime options:
```bash
TSAN_OPTIONS=second_deadlock_stack=1:halt_on_error=0 ./app
```

### 5.3 UndefinedBehaviorSanitizer (UBSan)

```bash
-fsanitize=undefined -g
# More specific subsets:
-fsanitize=signed-integer-overflow,null,alignment,shift,bounds
```

Detects:
- Signed integer overflow / underflow
- Null pointer dereference
- Misaligned pointer access
- Shift by ≥ width or negative
- Array index out of bounds (when compile-time detectable)
- Invalid enum cast
- Reaching `__builtin_unreachable()`
- Calling through a function pointer with the wrong type
- Integer division by zero

Overhead: minimal (5–20% runtime, no extra memory).
Can be combined with ASan: `-fsanitize=address,undefined`.

Runtime options:
```bash
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 ./app
```

### 5.4 LeakSanitizer (LSan)

```bash
-fsanitize=leak   # standalone (Linux only)
# on Linux ASan integrates LSan; enable with:
ASAN_OPTIONS=detect_leaks=1 ./asan_binary
```

Detects:
- Memory never freed at program exit.
- Distinguishes "definitely lost" (no pointer to block exists) from
  "indirectly lost" (only reachable through another lost block).

macOS: LSan is integrated into ASan on macOS but with some limitations;
  use `MallocStackLogging` + `leaks` tool for full analysis.

### 5.5 MemorySanitizer (MSan) — Clang only, Linux only

```bash
clang++ -fsanitize=memory -fno-omit-frame-pointer -g
```

Detects:
- Use of uninitialised memory.
- Including reading uninitialised values in conditions, function arguments.

Mechanism: shadow bit per memory byte; propagates "taint".

Overhead: ~3× memory, ~3× runtime.

Critical constraint: **every library** in the process must be compiled
with MSan; otherwise false positives from uninstrumented code.

Practical setup: use a pre-built MSan-instrumented stdlib
(`libc++-msan`) or build from source with MSan.

### 5.6 Combining sanitizers

| Combo | Valid? |
|-------|--------|
| ASan + UBSan | ✅ common, recommended |
| ASan + LSan | ✅ LSan is part of ASan on Linux |
| TSan alone | ✅ |
| ASan + TSan | ❌ incompatible |
| MSan alone | ✅ (Clang, Linux) |
| MSan + ASan | ❌ incompatible |

```bash
# Recommended dev build:
g++ -std=c++20 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -fsanitize-address-use-after-scope \
    main.cpp -o app
```

---

## 6. Static Analysis

Static analysis checks code **without running it**.

### 6.1 Compiler built-in analysis

```bash
# GCC/Clang: static analyser (pattern matching, not whole-program)
clang++ --analyze main.cpp
g++ -fanalyzer main.cpp
```

### 6.2 clang-tidy

clang-tidy is a linter built on the Clang AST.  It uses a
`compile_commands.json` to understand how each file is compiled.

```bash
# Run on a file using compilation database:
clang-tidy -p build/ main.cpp

# Enable specific check groups:
clang-tidy -checks='modernize-*,readability-*,bugprone-*,performance-*' main.cpp

# Auto-fix issues:
clang-tidy -fix -checks='modernize-use-nullptr' main.cpp

# Suppress a specific check in code:
// NOLINT(readability-identifier-naming)
```

**Check categories:**

| Category | What it catches |
|---|---|
| `modernize-*` | `nullptr`, range-for, `override`, trailing return types |
| `readability-*` | naming, simplifications, redundant code |
| `bugprone-*` | integer overflow, use-after-move, suspicious semicolons |
| `performance-*` | unnecessary copies, inefficient string ops |
| `cert-*` | SEI CERT C++ coding standards |
| `cppcoreguidelines-*` | C++ Core Guidelines |
| `hicpp-*` | High Integrity C++ |
| `misc-*` | Miscellaneous best practices |

Configure via `.clang-tidy` YAML file at the project root:
```yaml
Checks: 'modernize-*,bugprone-*,-modernize-use-trailing-return-type'
WarningsAsErrors: 'bugprone-*'
HeaderFilterRegex: '.*'
FormatStyle: 'file'
```

### 6.3 cppcheck

A static analyser that uses its own parser (not Clang).  Often catches
different things to clang-tidy.

```bash
cppcheck --enable=all --inconclusive --std=c++20 src/
cppcheck --project=build/compile_commands.json
```

Common findings: null pointer dereference, resource leaks, out-of-bounds,
redundant conditions, deprecated functions, integer overflow.

### 6.4 include-what-you-use (IWYU)

Checks that every header you include is actually needed, and that headers
you use are directly included (not just transitively available).

```bash
include-what-you-use -p build/ main.cpp
# Apply suggested changes:
fix_includes.py < iwyu.out
```

### 6.5 CMake integration

```cmake
# clang-tidy on every compile:
set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=-*,modernize-*,bugprone-*)

# cppcheck:
set(CMAKE_CXX_CPPCHECK cppcheck --enable=warning,performance)

# include-what-you-use:
set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE include-what-you-use)
```

---

## 7. compile_commands.json

A JSON file containing the exact compile invocation for every file.
Required by clangd, clang-tidy, and many editor integrations.

### 7.1 Generation

```bash
# CMake (any generator):
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# Ninja directly:
ninja -C build -t compdb > compile_commands.json
# Without CMake — bear intercepts compiler calls:
bear -- make
```

### 7.2 Format

```json
[
  {
    "directory": "/home/user/project/build",
    "command": "g++ -std=c++20 -O2 -I../include -c ../src/main.cpp -o main.o",
    "file": "/home/user/project/src/main.cpp"
  }
]
```

### 7.3 clangd

A language server (LSP) that reads `compile_commands.json` to provide:
- Accurate go-to-definition across headers
- Real-time diagnostics with exact compiler flags
- Code completion with correct includes
- Rename/refactoring

Put a symlink to `compile_commands.json` in the project root or configure
`clangd` with `--compile-commands-dir=build`.

---

## 8. Build Performance

### 8.1 ccache

Caches object files keyed on preprocessed source hash.  Subsequent builds
that only change unrelated files (or after a `make clean`) skip recompilation.

```bash
brew install ccache   # macOS
apt install ccache    # Linux

# Use with CMake:
cmake -B build -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

# Direct invocation:
ccache g++ -std=c++20 -O2 main.cpp -o main

# Stats:
ccache --show-stats
ccache --zero-stats
```

Hit rate after a warm cache: typically 95%+ on incremental rebuilds.
Cache size default: 5 GB.  Configure: `ccache --set-config max_size=20G`.

### 8.2 Precompiled headers (PCH)

Parses a header once and saves the parsed AST as a `.gch`/`.pch` file.
Subsequent TUs that include the header load the binary blob instead.

Best candidates: very stable, heavily included headers (e.g., STL or Boost).

```cmake
# CMake ≥ 3.16:
target_precompile_headers(my_app PRIVATE
    <vector>
    <string>
    <unordered_map>
    "src/common.h"
)
# Share a PCH between targets:
target_precompile_headers(my_lib REUSE_FROM base_target)
```

Manual (GCC):
```bash
g++ -std=c++20 -O2 -x c++-header common.h -o common.h.gch
g++ -std=c++20 -O2 main.cpp  # automatically picks up common.h.gch
```

### 8.3 Unity (jumbo) builds

Merge multiple `.cpp` files into one TU for compilation.  Reduces:
- Repeated parsing of common includes
- Linker symbol visibility issues

```cmake
set_target_properties(my_lib PROPERTIES UNITY_BUILD ON)
set_target_properties(my_lib PROPERTIES UNITY_BUILD_BATCH_SIZE 8)
```

Caveats: static/anonymous namespace symbols in different TUs may collide;
some ODR violations become visible.

### 8.4 Link-Time Optimisation (LTO)

The linker sees all object files simultaneously and can:
- Inline across TU boundaries
- Eliminate dead code that spans TUs
- Perform whole-program devirtualisation

```cmake
set_property(TARGET my_app PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
# Or manually:
cmake -B build -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
```

Manual flags:
```bash
g++ -O2 -flto=auto main.cpp lib.cpp -o app    # thin LTO in GCC 10+
clang++ -O2 -flto=thin main.cpp lib.cpp -o app # ThinLTO (incremental)
```

**Thin LTO** (Clang): only imports function summaries; incremental,
parallelisable.  Preferred for large projects.
**Full LTO**: reads entire IR; slower link but more aggressive.

---

## 9. Linkers

| Linker | Speed | Notes |
|--------|-------|-------|
| `ld.bfd` | baseline | GNU default; slowest |
| `ld.gold` | 2× faster | Better at C++; `-fuse-ld=gold` |
| `lld` | 5–10× faster | LLVM linker; ThinLTO support; `-fuse-ld=lld` |
| `mold` | 10–20× faster | Newest; parallel; `-fuse-ld=mold` |

```bash
# Select linker:
g++ -fuse-ld=lld main.o -o app
cmake -B build -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
```

Useful linker flags:
```bash
-Wl,--as-needed           # skip unused shared libs
-Wl,--gc-sections         # remove dead sections (needs -ffunction-sections -fdata-sections)
-Wl,-dead_strip           # macOS equivalent of --gc-sections
-Wl,--strip-debug         # strip debug symbols from final binary
-Wl,--print-map           # dump linker map (what went where)
```

---

## 10. Cross-Compilation

```cmake
# toolchain.cmake — passed via -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_SYSROOT /opt/sysroots/aarch64)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
```

---

## Examples

| File | Topic |
|------|-------|
| `CMakeLists.txt` | Full modern CMake project with sanitizer options, presets |
| `01_asan_heap.cpp` | ASan: heap buffer overflow, use-after-free, double-free |
| `02_asan_stack_leak.cpp` | ASan: stack overflow, use-after-scope, LeakSanitizer |
| `03_ubsan.cpp` | UBSan: signed overflow, null deref, misaligned, shift |
| `04_tsan.cpp` | TSan: data race, fixed with atomic and mutex |
| `05_msan.cpp` | MSan: uninitialised reads (Clang + Linux) |
| `06_compiler_warnings.cpp` | -Wall/-Wextra patterns and fixes |
| `07_clang_tidy.cpp` | Patterns caught by modernize/bugprone/performance |
| `08_build_modes.cpp` | Debug/Release, NDEBUG, assert, LTO effects |
| `09_pch_unity.cpp` | Precompiled headers, unity build, compile_commands |
| `10_toolchain.cpp` | ccache, compile_commands.json, linker flags, cross-compile |
