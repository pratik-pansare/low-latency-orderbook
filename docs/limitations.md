# Project Atlas: Known Limitations & Future Roadmap

## 1. Current Architecture Scope

Atlas is intentionally designed as an ultra-low-latency, single-threaded matching core with asynchronous lock-free egress. While research-grade and benchmark-driven, certain enterprise exchange features are out of scope for the current milestone:

### 1.1 Single-Threaded Matching Core
- **Design Rationale**: In modern HFT and electronic exchanges (e.g., CME, NASDAQ, LSE), the matching engine hot path for a single symbol is strictly single-threaded to eliminate locking overhead, cache ping-pong, and non-deterministic race conditions.
- **Limitation**: Throughput is bounded by single-core IPC and clock speed rather than multi-core scale-out.
- **Future Milestone**: Symbol-based partitioning (sharding order books across dedicated CPU-pinned worker threads).

### 1.2 Memory Capacity Boundaries
- The optimized backend relies on fixed-capacity pre-allocated pools (`FixedPool`) to ensure deterministic memory behavior.
- If order count exceeds `config.max_orders` or active levels exceed `config.max_price_levels`, incoming orders receive `RejectReason::BookFull`.
- **Future Milestone**: Implement dynamic slab expansion (allocating additional contiguous chunks asynchronously on a background thread when pool reaches 80% watermark).

### 1.3 Floating-Point Free Design
- Prices are strictly integer ticks (`int64_t`) and quantities are discrete units (`uint64_t`). Fractional shares or floating-point pricing are prohibited to eliminate rounding and precision errors.

---

## 2. Advanced Performance Roadmap

1. **Kernel Bypass Networking**:
   - Integration with Solarflare OpenOnload or DPDK for sub-microsecond UDP multicast market data feed ingestion and TCP order entry.
2. **SIMD-Accelerated Price Level Scanning**:
   - AVX-512 / ARM NEON vectorized matching for dense price ladder ranges.
3. **Hardware Performance Counters**:
   - Automated profiling with Linux `perf` events (L1-dcache-load-misses, branch-misses, cycle counters) integrated directly into CI/CD pipelines.
4. **Live State Migration**:
   - Online migration protocol allowing transparent transition of live resting orders from the sparse intrusive ladder to a dense bitmap ladder during high-volatility regimes.
