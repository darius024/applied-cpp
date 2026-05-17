# Performance Engineering in C++

Low-latency C++ is a first-class skill in quantitative finance. Every
microsecond saved on the order path can improve fill rates and reduce
adverse selection. This module covers the hardware and compiler
mechanisms that determine performance, and the tools to measure them.

---

## CPU architecture essentials

Modern CPUs are far more complex than the "execute one instruction"
model. Understanding them is the prerequisite for performance work.

### Pipeline and out-of-order execution
The CPU breaks each instruction into micro-ops (decode, execute, writeback)
and processes many simultaneously. Out-of-order execution lets the CPU
reorder independent instructions and execute them in parallel. Long
dependency chains (data hazards) stall the pipeline.

### Clock cycles and latency vs throughput
- **Latency**: how many cycles until the result of an instruction is ready.
- **Throughput**: how many such instructions can start per cycle.
- Integer add: 1 cycle latency, 4/cycle throughput.
- L1 cache load: ~4 cycles latency.
- L3 cache load: ~40 cycles latency.
- RAM access: ~200+ cycles latency.

### The memory hierarchy

| Level  | Size (typical) | Latency   | Bandwidth     |
|--------|----------------|-----------|---------------|
| L1d    | 32–64 KiB      | ~4 cycles | ~1 TB/s       |
| L2     | 256 KiB–1 MiB  | ~12 cycles| ~400 GB/s     |
| L3     | 8–64 MiB       | ~40 cycles| ~200 GB/s     |
| RAM    | GiBs           | ~200 cycles| ~50 GB/s     |
| NVMe   | TiBs           | ~100 µs   | ~7 GB/s       |

**Rule**: the fastest code is code that fits in L1. Everything else is
waiting for the memory system.

### Cache lines
The cache operates in 64-byte blocks called **cache lines**. When you
access any byte in a line, the entire 64 bytes are fetched. Implications:
- Accessing elements sequentially reuses lines already in cache (fast).
- Accessing every 64th byte causes one cache miss per access (slow).
- A struct that spans two cache lines causes two fetches.
- `sizeof(double) * 8 == 64` — eight doubles fit in one cache line.

---

## Cache effects

### Spatial and temporal locality
- **Spatial**: access memory addresses near ones you've already accessed.
  Iterating a contiguous array is the canonical example.
- **Temporal**: reuse the same memory soon after accessing it.
  A hot inner loop that repeatedly reads the same small dataset.

### False sharing
Two threads write to different variables that happen to share a cache
line. Each write invalidates the entire line in the other core's cache,
causing cache-line ping-pong:

```cpp
// SLOW: x and y are in the same cache line
struct Shared { int x; int y; };

// FAST: each variable on its own line
struct Aligned {
    alignas(64) int x;
    alignas(64) int y;
};
```

### Array of Structures (AoS) vs Structure of Arrays (SoA)

```cpp
// AoS — common OOP layout; poor for vectorized processing
struct Quote { double bid; double ask; double mid; int32_t size; };
Quote quotes[N]; // quotes[i].bid, quotes[i].ask are adjacent

// SoA — cache-friendly when processing one field across all elements
struct QuoteBook {
    double bid[N];
    double ask[N];
    double mid[N];
    int32_t size[N];
};
```

A VWAP loop over `bid` in SoA layout touches only the `bid` array,
perfectly using every cache line loaded. AoS wastes 3/4 of each line.

---

## Memory layout and alignment

The compiler inserts **padding bytes** to satisfy alignment requirements:
each member's address must be a multiple of its alignment (usually its
size). The struct's total size is padded to a multiple of its largest member.

```cpp
struct Bad  { char a; int b; char c; };  // 12 bytes — 5 bytes wasted
struct Good { int b; char a; char c; };  //  8 bytes — 2 bytes wasted
```

Rule: sort members largest-to-smallest (or smallest-to-largest) to
minimise internal padding.

`alignas(N)` forces alignment to N bytes. Use it to:
- Align a struct to a cache line (`alignas(64)`).
- Align a buffer for SIMD instructions (`alignas(32)` for AVX).

`static_assert(sizeof(T) == expected)` locks the layout in — prevents
accidental padding growth as the struct evolves.

---

## Branch prediction

The CPU's branch predictor guesses whether a conditional branch will be
taken and starts executing that path speculatively. A misprediction
flushes the pipeline — typically 10–20 cycles penalty.

### What predicts well
- Loops with known fixed counts.
- Branches that always go the same way (always-taken, always-not-taken).
- Branches with a stable pattern (TTTF TTTF…).

### What predicts poorly
- Data-dependent branches over random data (e.g., checking stock prices).
- Virtual dispatch to an ever-changing set of derived types.

### Branchless patterns
Replacing a branch with arithmetic or conditional moves (CMOVcc):

```cpp
// branch version — potential misprediction
int abs_val = x < 0 ? -x : x;

// branchless — compiler turns this into cmov
int mask = x >> 31;        // -1 if negative, 0 if positive
int abs_val = (x ^ mask) - mask;

// or simply: the compiler handles abs() without branches
int abs_val = std::abs(x);
```

### C++20 [[likely]] / [[unlikely]]
```cpp
if (order.qty <= 0) [[unlikely]] {
    handle_error(order);
    return;
}
process(order); // compiler keeps this on the hot path
```

`[[likely]]` causes the compiler to lay out the branch so the fast path
avoids a taken branch (which is more expensive than a not-taken branch
on many CPUs).

---

## SIMD and auto-vectorization

SIMD (Single Instruction, Multiple Data) instructions process multiple
values in parallel. A 256-bit AVX register holds 4 doubles at once.

The compiler performs **auto-vectorization** at `-O2`/`-O3` when it can
prove:
1. No aliasing: pointers do not overlap. Use `__restrict__` or `restrict`.
2. No data dependency between iterations.
3. Aligned data (or the compiler inserts runtime alignment checks).

```cpp
void add_pnl(const double* __restrict__ a,
             const double* __restrict__ b,
             double* __restrict__ out, int n) {
    for (int i = 0; i < n; ++i)
        out[i] = a[i] + b[i]; // auto-vectorized with -O2
}
```

`std::assume_aligned<N>(ptr)` (C++20) tells the compiler the pointer is
aligned to N bytes, enabling vectorised loads without peel loops.

Check vectorization: `g++ -O3 -fopt-info-vec-optimized file.cpp`
(or `-Rpass=loop-vectorize` with Clang).

---

## Compiler optimisations

### Optimisation flags

| Flag  | Effect                                                     |
|-------|------------------------------------------------------------|
| `-O0` | No optimisation — fastest compile; easy debugging          |
| `-O1` | Basic optimisations; no inlining of large functions        |
| `-O2` | Full safe optimisations; used in most production builds    |
| `-O3` | Aggressive; enables auto-vectorisation, loop unrolling     |
| `-Os` | Optimise for size (smaller code → better I-cache fit)      |
| `-Ofast`| `-O3` + unsafe math (`-ffast-math`) — not IEEE-safe     |

`-ffast-math` allows reordering of FP operations and ignores NaN/Inf —
useful for VWAP/PnL loops if you trust your data.

### Inlining
Inlining eliminates function-call overhead and enables further
optimisations (constant propagation, dead-code elimination).
`[[gnu::always_inline]]` / `__forceinline` forces inlining.
`[[gnu::noinline]]` prevents it (useful to keep profiler stacks clean).

### Link-Time Optimisation (LTO)
`-flto` lets the compiler inline and optimise across translation units.
Critical for hot-path code split across multiple `.cpp` files.

### Profile-Guided Optimisation (PGO)
1. `g++ -fprofile-generate` — compile with instrumentation.
2. Run with representative workload.
3. `g++ -fprofile-use` — recompile using the profile.
The compiler aligns hot code, reorders branches, and inlines based on
actual execution data.

---

## Latency measurement

### rdtsc — the hardware cycle counter
`__builtin_ia32_rdtsc()` (x86) reads the Time Stamp Counter — a 64-bit
register incremented every CPU cycle. Subtract two readings, divide by
frequency, to get nanoseconds. Sub-nanosecond resolution; near-zero
overhead. **Pitfall**: out-of-order execution may reorder reads with the
work; use `__builtin_ia32_rdtscp` or fences.

On macOS ARM (Apple Silicon): use `mach_absolute_time()`.
POSIX portable: `clock_gettime(CLOCK_MONOTONIC)` — ~20–30 ns overhead.

### Measurement pitfalls
- **Cold-start effect**: first run loads cache; warm up before recording.
- **Turbo / frequency scaling**: pin the governor to performance mode.
- **Compiler dead-code elimination**: if the result of timed code is unused,
  the compiler may remove it. Use `DoNotOptimize` (see `09_benchmarking.cpp`).
- **Memory layout effects**: place hot data in the right cache state before
  timing.

### Percentile analysis
For latency profiling, median is misleading. Report:
- p50 (median), p95, p99, p99.9 — the tail matters most in trading.
- Use a sorted sample array or an online algorithm (HDR histogram).

---

## Memory allocators

`malloc` / `new` are general-purpose and have hidden costs:
- Thread synchronisation (lock or lock-free internals).
- Search for a free block of the right size.
- System call (`brk`/`mmap`) when the pool is exhausted.

In latency-sensitive code, prefer:

### `std::pmr` (C++17 — Polymorphic Memory Resources)
```cpp
#include <memory_resource>

// monotonic_buffer_resource: bump-pointer allocator; never frees.
// Zero overhead per allocation after setup.
std::byte buf[4096];
std::pmr::monotonic_buffer_resource pool{buf, sizeof(buf)};
std::pmr::vector<double> prices{&pool};

// synchronized_pool_resource: fixed-size blocks, thread-safe.
// unsynchronized_pool_resource: single-threaded, lower overhead.
```

### Arena / stack allocator
Pre-allocate a large buffer at startup; bump a pointer for each
allocation; reset to zero at the end of each order event. GC-like but
deterministic and cache-hot.

### Custom allocator performance rules
- Know the allocation size at compile time → use a pool.
- Object lifetime matches a scope/event → use monotonic + reset.
- Many small allocations of the same type → use a slab allocator.

---

## Lock-free programming

### `std::atomic` memory orders

| Order               | Guarantees                                            |
|---------------------|-------------------------------------------------------|
| `memory_order_relaxed`  | No reordering; only atomicity                     |
| `memory_order_acquire`  | No reads/writes may move ABOVE this load          |
| `memory_order_release`  | No reads/writes may move BELOW this store         |
| `memory_order_acq_rel`  | Both; for read-modify-write ops (fetch_add, CAS)  |
| `memory_order_seq_cst`  | Full fence; most expensive; the default           |

### Compare-and-swap (CAS)
```cpp
T expected = current.load(memory_order_relaxed);
while (!current.compare_exchange_weak(expected, new_value,
                                       memory_order_release,
                                       memory_order_relaxed)) {
    // expected was updated to the actual value; retry
}
```

### Single-Producer Single-Consumer (SPSC) queue
The canonical lock-free pattern in HFT:
- One atomic head index (written by producer, read by consumer).
- One atomic tail index (written by consumer, read by producer).
- No CAS needed — only one thread writes each index.
- With correct memory ordering, no mutex needed.

### ABA problem
CAS on a pointer can succeed when it should fail if the value was
changed from A → B → A by another thread. Mitigations: tagged pointers
(embed a counter in the unused bits), hazard pointers, epoch reclamation.

---

## Benchmarking methodology

### Google Benchmark
The standard C++ microbenchmark library:
```cpp
#include <benchmark/benchmark.h>

static void BM_VwapCalc(benchmark::State& state) {
    std::vector<double> prices(state.range(0), 100.0);
    for (auto _ : state)
        benchmark::DoNotOptimize(vwap(prices));
}
BENCHMARK(BM_VwapCalc)->Range(64, 1<<16);
BENCHMARK_MAIN();
```
Link: `-lbenchmark -lpthread`

### Preventing dead-code elimination
Without `DoNotOptimize`, the compiler may prove that the output of a
benchmarked function is unused and delete it entirely:
```cpp
// Using volatile write (portable but restricts optimisation)
volatile double sink = result;

// Asm clobber (GCC/Clang):
asm volatile("" : "+r"(val)); // DoNotOptimize
asm volatile("" ::: "memory"); // ClobberMemory
```

### Microbenchmark pitfalls
- **Warmup**: run the loop at least once before recording.
- **Steady-state frequency**: disable turbo/powersave on the test machine.
- **Interference**: isolate CPUs with `isolcpus` / `taskset` on Linux.
- **Measurement overhead**: time a no-op loop and subtract.
- **Inlining across benchmark boundary**: `__attribute__((noinline))` the SUT.

---

## Profiling tools

| Tool       | Platform  | What it measures                              |
|------------|-----------|-----------------------------------------------|
| `perf`     | Linux     | CPU cycles, cache misses, branches, IPC       |
| `Instruments` | macOS  | CPU time, allocations, system calls           |
| `VTune`    | x86       | Micro-architectural analysis, pipeline stalls |
| `callgrind`| Linux     | Instruction counts, simulated cache misses    |
| `heaptrack`| Linux     | Heap allocation profiling                     |
| `perf stat`| Linux     | Hardware counter summary (one command)        |

Quick usage:
```
# Linux
perf stat -e cycles,instructions,cache-misses ./my_app
perf record -g ./my_app && perf report

# macOS
xcrun xctrace record --template "Time Profiler" --launch ./my_app
```

### Self-instrumentation
For production code where you cannot run a profiler:
- RAII timer: record start/end with `clock_gettime`; log to a ringbuffer.
- rdtsc sampling: read TSC at entry/exit of key functions; accumulate.
- `perf_event_open` (Linux): read hardware counters from within the program.

---

## Finance-specific performance patterns

### Order path optimisation
The **tick-to-trade** latency (market data received → order sent) is the
key metric. The hot path typically:
1. Deserialise the market data update.
2. Update the order book.
3. Run the pricing/alpha model.
4. Compute order parameters.
5. Serialise and send the order.

Each step should: avoid heap allocation, fit working set in L1/L2, use
SPSC queues between threads, and have zero lock contention.

### Order book data structures
A price-level array indexed by price tick (a flat array sorted by price)
is far faster than `std::map<double, int>` for a known price range:
- O(1) insertion/deletion by price.
- Contiguous memory → vectorisable sweep for VWAP/TWA.
- Prefetchable access pattern.

### Avoid copies on the hot path
Pass by `const&` or pointer. Use `std::string_view` / `std::span` for
views into existing buffers. Never `std::string` copy on the hot path.

---

## Examples

| File                       | Covers                                                       |
|----------------------------|--------------------------------------------------------------|
| `01_cache_effects.cpp`     | Sequential vs random access, false sharing, AoS vs SoA      |
| `02_memory_layout.cpp`     | Struct padding, member reordering, `alignas`, cache-line alignment |
| `03_branch_prediction.cpp` | Sorted vs unsorted branch, branchless abs/clamp, `[[likely]]` |
| `04_simd_vectorization.cpp`| Auto-vectorization conditions, `__restrict__`, `assume_aligned` |
| `05_compiler_hints.cpp`    | `[[likely]]`, `always_inline`, hot/cold, `__builtin_expect`  |
| `06_latency_timing.cpp`    | `clock_gettime`, RAII timer, latency histogram, rdtsc        |
| `07_allocators.cpp`        | `pmr::monotonic_buffer_resource`, pool, arena, vs malloc     |
| `08_lockfree.cpp`          | CAS loop, SPSC queue, acquire/release ordering               |
| `09_benchmarking.cpp`      | Manual harness, DoNotOptimize, warmup, stable timing         |
| `10_profiling.cpp`         | RAII instrumentation, rdtsc sampling, `perf_event_open`      |
