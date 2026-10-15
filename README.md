# Atlas: High-Performance C++20 Limit Order Book & Deterministic Market Replay Engine

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/Build-CMake%203.20%2B-green.svg)](https://cmake.org/)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan%20Clean-brightgreen.svg)]()
[![Tests](https://img.shields.io/badge/GoogleTest-24%2F24%20Passed-success.svg)]()
[![Throughput](https://img.shields.io/badge/Throughput-16.85M%20ops%2Fs-orange.svg)]()
[![License](https://img.shields.io/badge/License-MIT-purple.svg)]()

> **Atlas** is a research-grade, production-inspired limit order book (LOB) and deterministic market replay engine engineered in modern **C++20**. Designed for high-frequency trading (HFT) and quantitative infrastructure workloads, it features **zero hot-path dynamic memory allocations**, cache-conscious contiguous memory pools, intrusive doubly linked FIFO price levels, and deterministic state hash validation.

---

## ⚡ Benchmark Highlights (Apple Silicon ARM64, `-O3`)

Empirical results measured via `./build/atlas_benchmark` on Apple Clang 16.0.0 (Release Build):

| Benchmark Workload | Engine Implementation | Mean Latency | p50 | p90 | p99 | Throughput |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Order Insertion** (Resting GTC) | **Atlas Optimized (Pool + Intrusive)** | **41.6 ns** | **42.0 ns** | **42.0 ns** | **42.0 ns** | **16.85 Million ops/s** |
| Order Insertion (Resting GTC) | `std::map` Baseline | 104.4 ns | 42.0 ns | 84.0 ns | 167.0 ns | 7.35 Million ops/s |
| **Order Cancellation** ($O(1)$) | **Atlas Optimized (Backend B)** | **33.9 ns** | **41.0 ns** | **42.0 ns** | **83.0 ns** | **19.43 Million ops/s** |
| Order Cancellation | `std::map` Baseline | 50.7 ns | 42.0 ns | 83.0 ns | 167.0 ns | 14.56 Million ops/s |
| **Trade Matching** (Aggressive Sweep)| **Atlas Optimized (Backend B)** | **47.8 ns** | **42.0 ns** | **42.0 ns** | **84.0 ns** | **15.34 Million ops/s** |
| Trade Matching (Aggressive Sweep)| `std::map` Baseline | 59.3 ns | 42.0 ns | 83.0 ns | 125.0 ns | 13.03 Million ops/s |
| **Deterministic Market Replay** | **Atlas Optimized (Backend B)** | **-** | **-** | **-** | **-** | **20.47 Million events/s** |

- **Zero Hot-Path Allocations**: 100% elimination of heap allocations during matching via preallocated generational slab pools.
- **Tail Latency Reduction**: p99 insertion latency dropped from 167 ns to **42 ns** (**4.0x tail latency improvement**).
- **Throughput Boost**: **2.3x higher order insertion throughput** compared to the standard library baseline.

---

## 🏗️ Architecture Overview

The matching engine processes incoming order flow through a strictly deterministic sequence of state transitions:

```
 Incoming Order (GTC / FAK)
             │
             ▼
 ┌───────────────────────┐
 │   Input Validation    │───(Invalid / Duplicate)──► Emit OrderRejectedEvent
 └───────────────────────┘
             │ (Valid)
             ▼
 ┌───────────────────────┐
 │  Opposing Best Price  │───(No match / Price out of bounds)
 └───────────────────────┘                    │
             │ (Crossing Liquidity)           │
             ▼                                │
 ┌───────────────────────┐                    │
 │  Execute Maker Price  │                    │
 │  (Emit Trade Events)  │                    │
 └───────────────────────┘                    │
             │                                │
             ▼                                │
 ┌───────────────────────┐                    │
 │ Unfilled Remainder?   │◄───────────────────┘
 └───────────────────────┘
       │           │
 (GTC) │           │ (FAK)
       ▼           ▼
 ┌───────────┐ ┌──────────────────────────┐
 │ Rest in   │ │ Cancel Remainder         │
 │ Queue     │ │ (OrderCancelledEvent)    │
 └───────────┘ └──────────────────────────┘
       │
       ▼
 ┌────────────────────────────────────────┐
 │ Lock-Free SPSC Ring Buffer (alignas 64)│ ──► Downstream Market Data Gateway
 └────────────────────────────────────────┘
```

---

## 🔬 Core Engineering Innovations

### 1. Dual-Backend Architecture
- **Backend A (`OrderedMapOrderBook`)**: An idiomatic STL reference implementation using `std::map<Price, PriceLevel>` and `std::list<Order>`. Acts as the formal verification oracle.
- **Backend B (`OptimizedOrderBook`)**: High-performance engine utilizing pre-allocated generational pools, intrusive doubly-linked FIFO queues, and an active price-level ladder.

### 2. Preallocated Generational Memory Pool (`FixedPool`)
- Pre-allocates contiguous memory at engine startup, eliminating heap fragmentation and allocator locks.
- Uses **Generational Handles** (`{ uint32_t index; uint32_t generation; }`). When an order is freed, the generation counter increments, guaranteeing that stale lookups safely fail rather than causing dangling pointer corruption.

### 3. Intrusive Doubly-Linked FIFO Queues (`IntrusiveList`)
- Order nodes embed their own `prev`, `next`, and parent `PriceLevel*` pointers directly inside `OrderNode`.
- Removing an order during cancellation is strictly **$O(1)$** without container searching or node deallocation overhead.

### 4. Deterministic Market Replay & Cryptographic State Checksums
- Reads sequenced market events (Add, Cancel, Modify) from CSV.
- Computes a stable 64-bit FNV-1a checksum ($H(\text{Bids}, \text{Asks})$) verifying bit-exact state reproduction across backends.

### 5. Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
- Decouples the single-threaded matching core from network publication threads.
- Cache-line padded (`alignas(64)`) to completely avoid CPU false sharing.
- Uses C++20 atomics with strict `std::memory_order_release` and `std::memory_order_acquire` memory fences.

---

## 🛠️ Build & Run Guide

### Prerequisites
- Modern C++20 compiler (`clang++` $\ge$ 14, `g++` $\ge$ 11, or Apple Clang 15+)
- CMake 3.20+
- Git

### 1. Compile in Release Mode
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

### 2. Run Interactive Demonstration
```bash
./build/atlas_demo
```
*Visualizes live order placement, market depth ladders, aggressive price level sweeping, FAK cancellations, and SPSC ring buffer streaming.*

### 3. Run GoogleTest Suite (Unit & Differential Tests)
```bash
./build/atlas_tests
```
*Executes 24 test suites covering edge cases, price-time priority, multi-level sweeps, and randomized differential fuzzing against the `std::map` oracle.*

### 4. Run Performance Benchmark Suite
```bash
./build/atlas_benchmark
```
*Measures single-op latency distributions (Mean, p50, p90, p99, Max) and throughput across multiple workloads.*

### 5. Run Deterministic Replay Engine
```bash
# Run self-contained dual-backend replay comparison
./build/atlas_replay

# Generate 50,000 synthetic market events to CSV
./build/atlas_replay --generate workload.csv --count 50000 --seed 42

# Replay and verify bit-exact state parity across backends
./build/atlas_replay --compare workload.csv
```

### 6. Build with Memory Sanitizers (ASan & UBSan)
```bash
cmake -B build_asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_asan -j8
./build_asan/atlas_tests
```

---

## 🧪 Correctness & Testing Methodology

1. **State Invariant Enforcement**:
   - Every live order exists in exactly one price level queue.
   - Aggregate level quantity strictly equals $\sum \text{order.qty}$.
   - Non-crossing invariant: $\text{best\_bid} < \text{best\_ask}$ at all times.
   - Strict FIFO time priority within identical price ticks.
2. **Differential Fuzzing**:
   - Generates randomized streams of 10,000+ operations (Add, Cancel, Modify, FAK) executed concurrently across both backends.
   - Asserts identical trade executions, fills, cancellations, and bit-exact 64-bit state checksums after every single step.
3. **Memory Safety**:
   - Zero memory leaks, zero boundary violations, and zero undefined behavior under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## 📁 Repository Structure

```
├── core/
│   ├── types.hpp           # Strong types: Price, Qty, OrderId, Side, TimeInForce
│   ├── events.hpp          # TradeEvent, OrderAccepted, OrderResting, MatchResult
│   ├── config.hpp          # Venue policies (priority retention, pool sizing)
│   └── order.hpp           # Common Order representation
├── book/
│   ├── order_book_interface.hpp # Abstract base interface and FNV-1a checksum
│   ├── ordered_map_book.hpp     # Backend A: std::map reference engine
│   └── optimized_book.hpp       # Backend B: Intrusive pool & active ladder engine
├── memory/
│   ├── fixed_pool.hpp      # Cache-aligned generational object slab pool
│   ├── intrusive_list.hpp  # Zero-allocation doubly-linked FIFO queues
│   └── alloc_tracker.hpp   # Hot-path allocation instrumentation
├── market_data/
│   ├── depth.hpp           # Top-N depth snapshots and formatting
│   ├── incremental.hpp     # Level 2 incremental update notifications
│   └── spsc_ring_buffer.hpp# Cache-line aligned lock-free SPSC ring buffer
├── replay/
│   ├── event_record.hpp    # CSV event serialization and parsing
│   ├── synthetic_generator.hpp # Deterministic synthetic event generator
│   └── replay_main.cpp     # Deterministic replay CLI utility
├── benchmarks/
│   └── main_bench.cpp      # Latency percentile and throughput benchmark runner
├── tests/
│   ├── test_main.cpp       # GoogleTest main runner
│   ├── test_unit.cpp       # Typed unit tests and SPSC ring buffer tests
│   ├── test_invariants.hpp # Formal invariant assertion harness
│   └── test_differential.cpp # Randomized differential property testing
├── tools/
│   ├── generate_workload.py# Python workload generator
│   └── analyze_benchmarks.py# HTML/SVG benchmark report generator
├── docs/
│   ├── architecture.md     # Architecture specifications and lifecycles
│   ├── design_decisions.md # Data structure trade-offs and memory layout
│   ├── benchmarks.md       # Empirical benchmark methodology and findings
│   └── limitations.md      # Future roadmap (kernel bypass, SIMD)
└── examples/
    └── demo.cpp            # Interactive command-line demonstration
```

---

## 📄 License
This project is open-source under the [MIT License](LICENSE).  
Authored by **Pratik Pansare** ([GitHub](https://github.com/pratik-pansare)).
