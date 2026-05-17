cmake_minimum_required(VERSION 3.25)
project(build_tooling CXX)

# ─────────────────────────────────────────────────────────────────────
# Build & Tooling — demonstration CMakeLists.txt
#
# Covers modern CMake patterns:
#   - Target-based model (PUBLIC/PRIVATE/INTERFACE)
#   - Build type configuration
#   - Sanitizer options (toggled via cache variables)
#   - Compiler warnings as a reusable INTERFACE target
#   - clang-tidy / cppcheck integration
#   - Precompiled headers
#   - LTO
#   - compile_commands.json export
#   - install() rules
#
# Quick start:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
#   cmake --build build
#   ctest --test-dir build
# ─────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # -std=c++20, not -std=gnu++20

# Always export a compilation database — required by clangd and clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Sanitizer options ─────────────────────────────────────────────────
option(ENABLE_ASAN  "Enable AddressSanitizer + UBSan"  OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer"           OFF)
option(ENABLE_MSAN  "Enable MemorySanitizer (Clang)"   OFF)
option(ENABLE_LSAN  "Enable LeakSanitizer (Linux)"     OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan are mutually exclusive.")
endif()
if(ENABLE_ASAN AND ENABLE_MSAN)
    message(FATAL_ERROR "ASan and MSan are mutually exclusive.")
endif()

# ── INTERFACE target: warnings ────────────────────────────────────────
# Consuming targets link PRIVATE to get these flags without propagating
# them to downstream consumers.
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wold-style-cast
        -Wformat=2
        -Wimplicit-fallthrough
        -fno-omit-frame-pointer   # better stack traces with sanitizers
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /WX
    >
)

# ── INTERFACE target: sanitizers ──────────────────────────────────────
add_library(project_sanitizers INTERFACE)

if(ENABLE_ASAN)
    message(STATUS "ASan + UBSan enabled")
    target_compile_options(project_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fsanitize-address-use-after-scope
        -fno-omit-frame-pointer
    )
    target_link_options(project_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
endif()

if(ENABLE_TSAN)
    message(STATUS "TSan enabled")
    target_compile_options(project_sanitizers INTERFACE -fsanitize=thread)
    target_link_options(project_sanitizers INTERFACE -fsanitize=thread)
endif()

if(ENABLE_MSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MSan requires Clang.")
    endif()
    message(STATUS "MSan enabled")
    target_compile_options(project_sanitizers INTERFACE -fsanitize=memory -fno-omit-frame-pointer)
    target_link_options(project_sanitizers INTERFACE -fsanitize=memory)
endif()

if(ENABLE_LSAN AND NOT ENABLE_ASAN)
    message(STATUS "LSan enabled (standalone)")
    target_compile_options(project_sanitizers INTERFACE -fsanitize=leak)
    target_link_options(project_sanitizers INTERFACE -fsanitize=leak)
endif()

# ── Optional: static analysis during compilation ──────────────────────
option(ENABLE_CLANG_TIDY  "Run clang-tidy on every compile"  OFF)
option(ENABLE_CPPCHECK    "Run cppcheck on every compile"     OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE}"
            "--checks=-*,modernize-*,bugprone-*,readability-*,performance-*"
            "--warnings-as-errors=bugprone-*"
        )
    else()
        message(WARNING "clang-tidy not found; ENABLE_CLANG_TIDY ignored.")
    endif()
endif()

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK
            "${CPPCHECK_EXE}"
            "--enable=warning,performance,portability"
            "--std=c++20"
            "--suppress=missingIncludeSystem"
        )
    else()
        message(WARNING "cppcheck not found; ENABLE_CPPCHECK ignored.")
    endif()
endif()

# ── Helper macro: add a demo executable ──────────────────────────────
# Each example gets warnings + sanitizers automatically.
macro(add_demo name source)
    add_executable(${name} ${source})
    target_link_libraries(${name}
        PRIVATE project_warnings project_sanitizers
    )
endmacro()

# ── Demo targets ──────────────────────────────────────────────────────
add_demo(asan_heap    01_asan_heap.cpp)
add_demo(asan_stack   02_asan_stack_leak.cpp)
add_demo(ubsan        03_ubsan.cpp)
add_demo(tsan         04_tsan.cpp)
target_link_libraries(tsan PRIVATE Threads::Threads)  # pthreads
find_package(Threads REQUIRED)
add_demo(msan         05_msan.cpp)
add_demo(warnings     06_compiler_warnings.cpp)
add_demo(tidy_demo    07_clang_tidy.cpp)
add_demo(build_modes  08_build_modes.cpp)
add_demo(pch_unity    09_pch_unity.cpp)
add_demo(toolchain    10_toolchain.cpp)

# ── Precompiled header example (on pch_unity target) ─────────────────
# Uncomment to try PCH — CMake ≥ 3.16:
# target_precompile_headers(pch_unity PRIVATE <vector> <string> <cstdio>)

# ── LTO (off by default; enable for Release benchmarks) ───────────────
option(ENABLE_LTO "Enable Link-Time Optimisation" OFF)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err)
    if(ipo_ok)
        set_property(TARGET build_modes PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled on build_modes target")
    else()
        message(WARNING "LTO not supported: ${ipo_err}")
    endif()
endif()

# ── Install rules ─────────────────────────────────────────────────────
install(TARGETS asan_heap asan_stack ubsan tsan
    RUNTIME DESTINATION bin/build_tooling_demos
)

# ── CTest integration ─────────────────────────────────────────────────
enable_testing()
# Simple smoke test: run each demo and expect exit code 0.
foreach(demo asan_heap asan_stack ubsan build_modes pch_unity toolchain)
    add_test(NAME smoke_${demo} COMMAND ${demo})
endforeach()

# ─────────────────────────────────────────────────────────────────────
# Usage examples:
#
#   Debug + ASan + UBSan:
#     cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
#     cmake --build build
#     ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./build/asan_heap
#
#   TSan:
#     cmake -B build_tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
#     cmake --build build_tsan && ./build_tsan/tsan
#
#   Release with LTO:
#     cmake -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=ON
#     cmake --build build_rel
#
#   With clang-tidy:
#     cmake -B build -G Ninja -DENABLE_CLANG_TIDY=ON
#     cmake --build build 2>&1 | grep "warning:"
# ─────────────────────────────────────────────────────────────────────
