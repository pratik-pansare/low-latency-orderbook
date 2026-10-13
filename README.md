# Atlas: High-Performance C++20 Limit Order Book & Market Replay Engine

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.20+-green.svg)](https://cmake.org/)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan%20Clean-brightgreen.svg)]()
[![Tests](https://img.shields.io/badge/GoogleTest-24%2F24%20Passed-success.svg)]()
[![Throughput](https://img.shields.io/badge/Throughput-16.85M%20ops%2Fs-orange.svg)]()

**Atlas** is an ultra-low-latency limit order book (LOB) and deterministic market replay engine engineered in modern C++20 for high-frequency trading (HFT) and quantitative infrastructure workloads. It demonstrates advanced cache-conscious data structures, zero-allocation memory pools, intrusive FIFO queues, deterministic state verification, and rigorous differential correctness testing.

---

## Key Performance Metrics (Measured on Apple Silicon ARM64)

Empirical benchmarks run via `atlas_benchmark` on Apple Clang 16.0 (`-O3`):

| Operation | Implementation | Mean Latency | p50 | p90 | p99 | Throughput |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Order Insertion** | **Atlas Optimized (Pool + Intrusive)** | **41.6 ns** | **42.0 ns** | **42.0 ns** | **42.0 ns** | **16.85 M ops/s** |
| Order Insertion | `std::map` Baseline | 104.4 ns | 42.0 ns | 84.0 ns | 167.0 ns | 7.35 M ops/s |
| **Order Cancellation** | **Atlas Optimized** | **33.9 ns** | **41.0 ns** | **42.0 ns** | **83.0 ns** | **19.43 M ops/s** |
| Order Cancellation | `std::map` Baseline | 50.7 ns | 42.0 ns | 83.0 ns | 167.0 ns | 14.56 M ops/s |
| **Trade Matching** | **Atlas Optimized** | **47.8 ns** | **42.0 ns** | **42.0 ns** | **84.0 ns** | **15.34 M ops/s** |
| Trade Matching | `std::map` Baseline | 59.3 ns | 42.0 ns | 83.0 ns | 125.0 ns | 13.03 M ops/s |
| **Market Replay** | **Atlas Optimized** | **-** | **-** | **-** | **-** | **20.47 M events/s** |

> **Performance Summary**: Atlas achieves a **2.3x throughput increase** over the standard library baseline and reduces p99 tail insertion latency from 167 ns down to **42 ns** (**4x reduction**), with **zero heap allocations on the matching hot path**.

---

## Architectural Highlights

1. **Dual-Backend Architecture**:
   - **Backend A (`OrderedMapOrderBook`)**: Reference baseline using `std::map` (RB-trees) and `std::list` queues. Acts as the formal test oracle.
   - **Backend B (`OptimizedOrderBook`)**: Production-inspired engine utilizing preallocated generational slab pools (`FixedPool`), zero-allocation intrusive doubly linked FIFO queues, and an active price-level chain with $O(1)$ best-bid/best-ask lookup.
2. **Deterministic Price-Time Priority Matching**:
   - Good-Till-Cancelled (GTC) and Fill-And-Kill (FAK) order matching.
   - Strict maker execution pricing (trades execute at resting limit prices).
   - In-place quantity reduction priority retention vs. price/size expansion queue repositioning.
3. **Deterministic Market Replay Engine (`atlas_replay`)**:
   - Processes synthetic or recorded sequenced CSV event streams.
   - Computes a 64-bit FNV-1a state checksum ($H(\text{Bids}, \text{Asks})$) verifying bit-exact state parity across backends.
4. **Lock-Free SPSC Ring Buffer**:
   - Cache-line padded (64 bytes) bounded ring buffer with acquire/release memory ordering for zero-contention event streaming.
5. **Testing & Verification**:
   - **Unit Tests**: 24 comprehensive test suites covering order boundaries, FAK partial fills, and depth queries.
   - **Differential Fuzzing**: Randomized property testing running identical event streams against both engines with zero state divergence.
   - **Memory Sanitizers**: Validated 100% clean under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).

---

## Quick Start & Build Instructions

### Prerequisites
- Modern C++20 compiler (`clang++` $\ge$ 14 or `g++` $\ge$ 11)
- CMake 3.20+
- Git

### Build in Release Mode
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

### Run All Unit & Differential Tests
```bash
./build/atlas_tests
```

### Run Benchmarks
```bash
./build/atlas_benchmark
```

### Run Deterministic Replay Engine
```bash
# Run self-contained dual-backend replay comparison
./build/atlas_replay

# Generate 50,000 synthetic events to CSV
./build/atlas_replay --generate workload.csv --count 50000 --seed 42

# Replay and verify bit-exact state parity
./build/atlas_replay --compare workload.csv
```

### Build with AddressSanitizer & UndefinedBehaviorSanitizer
```bash
cmake -B build_asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_asan -j8
./build_asan/atlas_tests
```

---

## Directory Structure

```
├── core/             # Types, execution events, venue configuration, Order struct
├── book/             # IOrderBook, OrderedMapOrderBook, OptimizedOrderBook
├── memory/           # FixedPool, IntrusiveList, AllocationTracker
├── market_data/      # Top-N Depth snapshots, incremental updates, SPSC ring buffer
├── replay/           # Replay engine, EventRecord, CSV parser, synthetic generator
├── benchmarks/       # Latency percentile distributions, throughput benchmarks
├── tests/            # GoogleTest unit, invariant, and differential test suites
├── tools/            # Python workload generator and benchmark visualizer
├── docs/             # Architecture, benchmark reports, and design documentation
└── examples/         # Runnable CLI interactive demonstration
```

---

## License
MIT License. Authored by Pratik Pansare.
