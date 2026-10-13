# Project Atlas: Architecture & Interface Specification

## 1. System Overview

**Atlas** is a research-grade, low-latency limit order book (LOB) and deterministic market replay engine implemented in modern C++20. The system is designed to provide nanosecond-scale order matching, predictable memory access patterns, zero hot-path allocations, and deterministic replay capabilities.

The engine provides two interchangeable backend implementations sharing a common semantic contract:
- **Backend A (`OrderedMapOrderBook`)**: An idiomatic STL reference implementation using `std::map` (RB-trees) and `std::list` queues. Serves as the correctness baseline and differential test oracle.
- **Backend B (`OptimizedOrderBook`)**: A cache-conscious, pre-allocated engine using intrusive doubly linked FIFO queues, generational slot handles, a fixed-capacity memory pool, and an active price-level index with incremental depth aggregation.

---

## 2. Directory Layout

```
low-latency-orderbook/
├── CMakeLists.txt
├── core/
│   ├── types.hpp           # Strong types: Price, Qty, OrderId, TimestampNs, SeqNum, Side, TimeInForce
│   ├── events.hpp          # Execution events: TradeEvent, OrderAccepted, OrderResting, OrderFilled, etc.
│   ├── config.hpp          # Engine configuration & venue policies (self-trade, priority rules)
│   └── order.hpp           # Common order representation & order state
├── memory/
│   ├── fixed_pool.hpp      # Cache-aligned, fixed-capacity slab allocator with freelist & generation counters
│   ├── intrusive_list.hpp  # Zero-allocation doubly-linked intrusive list for FIFO price levels
│   └── alloc_tracker.hpp   # Allocation instrumentation and memory footprint reporting
├── book/
│   ├── order_book_interface.hpp # Abstract interface / semantic concept for LOB backends
│   ├── price_level.hpp     # Price level aggregate structure (total qty, order count, queue)
│   ├── ordered_map_book.hpp# Backend A: std::map reference implementation
│   └── optimized_book.hpp  # Backend B: Intrusive pool & ladder implementation
├── market_data/
│   ├── depth.hpp           # Top-N depth snapshot & level data
│   ├── incremental.hpp     # Level 2 incremental update notifications
│   └── spsc_ring_buffer.hpp# Cache-line padded lock-free single-producer single-consumer ring buffer
├── replay/
│   ├── event_record.hpp    # Sequenced input event representation
│   ├── event_reader.hpp    # Parsing synthetic and CSV event logs
│   ├── event_writer.hpp    # Deterministic event logging
│   └── replay_engine.hpp   # Deterministic replay driver with checksum validation
├── benchmarks/
│   ├── benchmark_runner.hpp# High-resolution benchmark framework (latency distributions, percentiles)
│   ├── micro_benchmarks.cpp# Add, cancel, modify, match, and depth benchmarks
│   └── workload_benchmarks.cpp# Dense, sparse, bursty, and replay workloads
├── tests/
│   ├── test_main.cpp       # Test runner
│   ├── test_unit.cpp       # Unit tests for all order types and edge cases
│   ├── test_invariants.hpp # Invariant assertion harness
│   └── test_differential.cpp# Randomized property tests comparing Backend A and Backend B
├── tools/
│   ├── generate_workload.py# Python workload generator
│   └── analyze_benchmarks.py# Benchmark statistics analysis and visualization
├── examples/
│   └── demo.cpp            # Command-line demonstration
└── docs/
    ├── architecture.md     # Architecture and component lifecycle
    ├── design_decisions.md # Data structure trade-offs & cache considerations
    ├── benchmarks.md       # Empirical benchmark results and methodology
    └── limitations.md      # Edge cases and future work
```

---

## 3. Core Data Types and Public Interface

### 3.1 Type Definitions
- `Price`: `int64_t` representing price ticks (e.g., $100.25 at $0.01 tick size = 10025). No floating point.
- `Qty`: `uint64_t` representing discrete units / shares.
- `OrderId`: `uint64_t` unique order identifier.
- `SeqNum`: `uint64_t` monotonically increasing engine sequence number.
- `TimestampNs`: `uint64_t` nanosecond timestamp.
- `Side`: `enum class Side : uint8_t { Buy, Sell };`
- `TimeInForce`: `enum class TimeInForce : uint8_t { GTC, FAK };`
  - **GTC (Good-Till-Cancelled)**: Fills immediately against executable resting orders; any unfilled quantity rests in the book.
  - **FAK (Fill-And-Kill)**: Fills immediately against executable resting orders; any unfilled quantity is cancelled immediately without resting.

### 3.2 Matching Semantics and Execution Rules
1. **Price-Time Priority**: 
   - Higher buy prices take priority over lower buy prices.
   - Lower sell prices take priority over higher sell prices.
   - At the same price level, orders are matched strictly FIFO (arrival sequence).
2. **Execution Price**:
   - The execution occurs at the **maker's price** (the resting order's price).
   - An aggressive Buy limit order never executes above its limit price.
   - An aggressive Sell limit order never executes below its limit price.
3. **Modification Rules**:
   - **Price Change**: Order loses time priority (moved to tail of new price level).
   - **Quantity Increase**: Order loses time priority (moved to tail of price level).
   - **Quantity Reduction**: By default (configurable venue policy), retains time priority at its current queue position. Aggregate price level quantity is decremented.
4. **Invalid Input Handling**:
   - `qty == 0` or `price <= 0`: Rejected immediately (`RejectReason::InvalidQuantity` or `InvalidPrice`).
   - Duplicate `OrderId`: Rejected (`RejectReason::DuplicateOrderId`).
   - Cancel/Modify missing `OrderId`: Rejected (`RejectReason::OrderNotFound`).

---

## 4. State Invariants

The following invariants must hold true after every operation:
1. **Queue Price Consistency**: Every order resting in a price level has `order.price == level.price`.
2. **Aggregate Quantity Conservation**: For every price level, `level.total_qty == sum(orders.qty)`.
3. **Aggregate Order Count Conservation**: For every price level, `level.order_count == len(orders)`.
4. **Index Bijectivity**: Every live order in the order lookup index exists in exactly one price level queue, and every order in a price level queue has a valid entry in the index.
5. **Non-Crossing Invariant**: `best_bid < best_ask` whenever both sides are non-empty. No executable liquidity remains crossed after an aggressive order finishes processing.
6. **Non-Negative Quantities**: All quantities and prices are positive and non-overflowing.
7. **Deterministic Checksum**: For any order book state, a checksum hash $H(S) = \text{hash}(\text{Bids}, \text{Asks})$ must match between Backend A and Backend B after identical event streams.
