#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Precompiled headers (PCH), unity builds, and compile_commands.json.
//
// Problem: large C++ projects spend most compile time repeatedly parsing
// the same headers (STL, Boost, internal frameworks) in every TU.
// PCH + unity builds attack this from different angles.
//
// Compile:
//   g++ -std=c++20 -O2 09_pch_unity.cpp -o pch && ./pch
//
// PCH manual demo:
//   # Step 1: compile the header into a .gch binary:
//   g++ -std=c++20 -O2 -x c++-header pch_demo_header.h -o pch_demo_header.h.gch
//   # Step 2: compile the TU — GCC/Clang automatically picks up .gch:
//   g++ -std=c++20 -O2 main.cpp
//
// CMake PCH (≥ 3.16):
//   target_precompile_headers(my_app PRIVATE <vector> <string> "common.h")
//
// Unity build:
//   Set UNITY_BUILD property ON — CMake merges TUs automatically.
//   set_target_properties(my_lib PROPERTIES UNITY_BUILD ON)
// ─────────────────────────────────────────────────────────────────────

static void section(const char* t) { std::printf("\n── %s ─────────\n", t); }

// ── PART 1: PCH motivation — count repeated header parsing time ────────
//
// Without PCH: each TU that includes <vector><string><algorithm> parses
// ~50-100K lines of header templates from scratch.
//
// With PCH: the first TU parses those headers and saves a binary AST
// snapshot.  Subsequent TUs load the snapshot (~10× faster than parsing).
//
// Rough numbers on a modern machine:
//   #include <vector><string><algorithm><unordered_map> → ~80 ms per TU
//   With PCH: → ~8 ms per TU (10 TUs: 800 ms → 80 ms)
//
static void demo_pch_motivation()
{
    section("PCH motivation — parsing overhead");
    std::puts("  Without PCH: each TU re-parses all included headers");
    std::puts("  <vector>+<string>+<algorithm> ≈ 80 ms parse time each TU");
    std::puts("  10 TUs × 80 ms = 800 ms just for headers");
    std::puts("  With PCH:    10 TUs × 8 ms  = 80 ms  (10× speedup on headers)");
    std::puts("");
    std::puts("  CMake: target_precompile_headers(target PRIVATE <vector> <string>)");
    std::puts("  Share across targets: target_precompile_headers(B REUSE_FROM A)");
}

// ── PART 2: PCH caveats ───────────────────────────────────────────────
//
// PCH must be the FIRST include in every TU (or CMake inserts it for you).
// The PCH is invalidated if any included header changes — full rebuild.
// Only include truly stable, rarely-changing headers in the PCH.
//
static void demo_pch_caveats()
{
    section("PCH caveats");
    std::puts("  1. PCH must appear before any other includes.");
    std::puts("     CMake's target_precompile_headers injects it automatically.");
    std::puts("  2. Any change to a PCH-included header invalidates the PCH.");
    std::puts("     → include only stable headers: STL, Boost, OS APIs.");
    std::puts("  3. Compiler must be consistent: same flags for PCH and TU.");
    std::puts("  4. PCH is compiler-specific: GCC .gch, Clang .pch/module.");
    std::puts("  5. C++20 Modules are the long-term replacement for PCH.");
}

// ── PART 3: Unity build ────────────────────────────────────────────────
//
// A unity (jumbo) build merges N TUs into one compilation unit.
// The merged TU's common headers are parsed only once — like PCH but
// achieved by concatenation rather than caching.
//
// CMake does this automatically:
//   set_target_properties(my_lib PROPERTIES UNITY_BUILD ON)
//   set_target_properties(my_lib PROPERTIES UNITY_BUILD_BATCH_SIZE 8)
//
// Caveats:
//   - static/anonymous namespace variables may collide if multiple TUs
//     define the same name.
//   - #include-order-sensitive code may break.
//   - Incremental builds are less granular: changing one file in a batch
//     forces recompilation of the whole batch.
//
// Demonstration: simulate what a unity build generates.
//
static void demo_unity_concept()
{
    section("unity build concept");
    std::puts("  Normal build:  lib_a.cpp, lib_b.cpp, lib_c.cpp — 3 invocations");
    std::puts("  Unity build:   unity_1.cpp (includes all three) — 1 invocation");
    std::puts("  Generated unity file (auto, do not edit):");
    std::puts("    // CMakeFiles/my_lib.dir/Unity/unity_0.cxx");
    std::puts("    #include \"/project/src/lib_a.cpp\"");
    std::puts("    #include \"/project/src/lib_b.cpp\"");
    std::puts("    #include \"/project/src/lib_c.cpp\"");
    std::puts("");
    std::puts("  Speedup: headers parsed once; linker sees fewer objects.");
    std::puts("  Caveat:  static/anon-namespace name collisions across TUs.");
}

// ── PART 4: compile_commands.json ────────────────────────────────────
//
// A JSON array where each entry describes how one source file is compiled.
// Required by: clangd (LSP), clang-tidy, include-what-you-use.
//
// Generation:
//   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
//   # Produces: build/compile_commands.json
//   # Symlink to project root for clangd:
//   ln -sf build/compile_commands.json compile_commands.json
//
// Without CMake (intercept build):
//   bear -- make       # https://github.com/rizsotto/Bear
//   compiledb make     # alternative
//
// Format (one entry per file):
//   {
//     "directory": "/project/build",
//     "command": "g++ -std=c++20 -O2 -I../include -c ../src/foo.cpp -o foo.o",
//     "file": "/project/src/foo.cpp"
//   }
//
static void demo_compile_commands()
{
    section("compile_commands.json");
    std::puts("  cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
    std::puts("  Produces: build/compile_commands.json");
    std::puts("  Symlink:  ln -sf build/compile_commands.json .");
    std::puts("");
    std::puts("  Tools that consume it:");
    std::puts("    clangd        — LSP: go-to-def, completions, diagnostics");
    std::puts("    clang-tidy    — linting with exact compiler flags");
    std::puts("    IWYU          — include-what-you-use header audit");
    std::puts("    clang-check   — light Clang static analysis");
}

// ── PART 5: forward declarations to reduce include overhead ───────────
//
// Every included header is fully parsed.  If you only need to declare a
// type (e.g., as a pointer parameter), a forward declaration is enough
// and avoids the full parse cost.
//
// Forward declaration is valid when you:
//   - Declare a pointer or reference to the type
//   - Declare a function taking/returning the type
// Forward declaration is NOT valid when you:
//   - Need the type size (creating an object, calling a method)
//   - Need the type's members or constructor
//
class OrderBook;   // forward declaration — no #include "order_book.h" needed

static void process_book(OrderBook* book); // declaration only

static void demo_fwd_decl()
{
    section("forward declarations");
    (void)process_book; // suppress unused warning
    std::puts("  'class OrderBook;' forward decl avoids including order_book.h");
    std::puts("  Valid uses: OrderBook*, &OrderBook, function declarations");
    std::puts("  Invalid:    OrderBook obj; ob.method(); sizeof(OrderBook)");
    std::puts("  Impact:     each avoided include saves header parse time");
}

static void process_book(OrderBook* /*book*/) { /* needs full definition */ }

// ── PART 6: IWYU (include-what-you-use) ──────────────────────────────
static void demo_iwyu()
{
    section("include-what-you-use (IWYU)");
    std::puts("  IWYU audits #includes: removes unneeded, adds missing direct ones.");
    std::puts("");
    std::puts("  Run:");
    std::puts("    include-what-you-use -p build/ source.cpp 2> iwyu.out");
    std::puts("    fix_includes.py < iwyu.out  # apply suggested changes");
    std::puts("");
    std::puts("  CMake integration:");
    std::puts("    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE include-what-you-use)");
    std::puts("");
    std::puts("  Common findings:");
    std::puts("    - You use std::sort but include <algorithm> transitively via <vector>");
    std::puts("    - You include <string> but only use std::string_view (needs <string_view>)");
}

int main()
{
    std::puts("=== PCH, unity builds, compile_commands.json ===");
    demo_pch_motivation();
    demo_pch_caveats();
    demo_unity_concept();
    demo_compile_commands();
    demo_fwd_decl();
    demo_iwyu();
    std::puts("\n=== see CMakeLists.txt in this directory for PCH + unity examples ===");
}
