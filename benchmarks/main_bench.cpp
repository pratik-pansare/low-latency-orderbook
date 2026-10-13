#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <random>

#include "book/ordered_map_book.hpp"
#include "book/optimized_book.hpp"
#include "memory/alloc_tracker.hpp"
#include "replay/synthetic_generator.hpp"

using namespace atlas::core;
using namespace atlas::book;
using namespace atlas::memory;
using namespace atlas::replay;

struct LatencyStats {
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    double max_ns{0.0};
    double mean_ns{0.0};
    double throughput_mops{0.0};
};

LatencyStats compute_stats(std::vector<uint64_t>& latencies_ns, double total_seconds) {
    if (latencies_ns.empty()) return {};
    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t n = latencies_ns.size();

    LatencyStats stats;
    stats.p50_ns = static_cast<double>(latencies_ns[n * 50 / 100]);
    stats.p90_ns = static_cast<double>(latencies_ns[n * 90 / 100]);
    stats.p99_ns = static_cast<double>(latencies_ns[n * 99 / 100]);
    stats.p999_ns = static_cast<double>(latencies_ns[n * 999 / 1000]);
    stats.max_ns = static_cast<double>(latencies_ns.back());

    double sum = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0.0);
    stats.mean_ns = sum / static_cast<double>(n);
    stats.throughput_mops = (static_cast<double>(n) / 1'000'000.0) / total_seconds;
    return stats;
}

void print_row(std::string_view name, const LatencyStats& s) {
    std::cout << std::left << std::setw(32) << name
              << std::right
              << std::setw(10) << std::fixed << std::setprecision(1) << s.mean_ns << " ns"
              << std::setw(10) << std::fixed << std::setprecision(1) << s.p50_ns << " ns"
              << std::setw(10) << std::fixed << std::setprecision(1) << s.p90_ns << " ns"
              << std::setw(10) << std::fixed << std::setprecision(1) << s.p99_ns << " ns"
              << std::setw(10) << std::fixed << std::setprecision(1) << s.max_ns << " ns"
              << std::setw(14) << std::fixed << std::setprecision(2) << s.throughput_mops << " M ops/s\n";
}

int main() {
    std::cout << "========================================================================================\n";
    std::cout << "                    ATLAS LOB PERFORMANCE BENCHMARK SUITE                               \n";
    std::cout << "========================================================================================\n\n";

    constexpr size_t WARMUP_OPS = 10'000;
    constexpr size_t BENCH_OPS = 200'000;

    EngineConfig config;
    config.max_orders = 1'000'000;
    config.max_price_levels = 50'000;

    // -------------------------------------------------------------
    // Workload 1: Insertion Latency (Non-crossing resting limit orders)
    // -------------------------------------------------------------
    std::cout << ">>> 1. INSERTION LATENCY (Resting Limit Orders)\n";
    std::cout << std::left << std::setw(32) << "Backend & Workload"
              << std::right
              << std::setw(13) << "Mean"
              << std::setw(13) << "p50"
              << std::setw(13) << "p90"
              << std::setw(13) << "p99"
              << std::setw(13) << "Max"
              << std::setw(16) << "Throughput" << "\n";
    std::cout << std::string(103, '-') << "\n";

    // Backend A (Ordered Map)
    {
        OrderedMapOrderBook bookA(config);
        // Warmup
        for (size_t i = 1; i <= WARMUP_OPS; ++i) {
            bookA.add_order(i, Side::Buy, 1000 + static_cast<Price>(i % 50), 10);
        }
        bookA.clear();

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            Price px = 1000 + static_cast<Price>(i % 100);
            auto start = std::chrono::steady_clock::now();
            bookA.add_order(i, Side::Buy, px, 10);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend A (std::map Baseline)", stats);
    }

    // Backend B (Optimized Intrusive Pool)
    {
        OptimizedOrderBook bookB(config);
        for (size_t i = 1; i <= WARMUP_OPS; ++i) {
            bookB.add_order(i, Side::Buy, 1000 + static_cast<Price>(i % 50), 10);
        }
        bookB.clear();

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            Price px = 1000 + static_cast<Price>(i % 100);
            auto start = std::chrono::steady_clock::now();
            bookB.add_order(i, Side::Buy, px, 10);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend B (Atlas Optimized)", stats);
    }
    std::cout << "\n";

    // -------------------------------------------------------------
    // Workload 2: Order Cancellation Latency (O(1) Handle Lookup & Unlink)
    // -------------------------------------------------------------
    std::cout << ">>> 2. CANCELLATION LATENCY\n";
    std::cout << std::left << std::setw(32) << "Backend & Workload"
              << std::right
              << std::setw(13) << "Mean"
              << std::setw(13) << "p50"
              << std::setw(13) << "p90"
              << std::setw(13) << "p99"
              << std::setw(13) << "Max"
              << std::setw(16) << "Throughput" << "\n";
    std::cout << std::string(103, '-') << "\n";

    // Backend A
    {
        OrderedMapOrderBook bookA(config);
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            bookA.add_order(i, Side::Buy, 1000 + static_cast<Price>(i % 100), 10);
        }

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            auto start = std::chrono::steady_clock::now();
            bookA.cancel_order(i);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend A (std::map Baseline)", stats);
    }

    // Backend B
    {
        OptimizedOrderBook bookB(config);
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            bookB.add_order(i, Side::Buy, 1000 + static_cast<Price>(i % 100), 10);
        }

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            auto start = std::chrono::steady_clock::now();
            bookB.cancel_order(i);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend B (Atlas Optimized)", stats);
    }
    std::cout << "\n";

    // -------------------------------------------------------------
    // Workload 3: Matching / Crossing Latency (Aggressive execution & trade generation)
    // -------------------------------------------------------------
    std::cout << ">>> 3. MATCHING & TRADE EXECUTION LATENCY\n";
    std::cout << std::left << std::setw(32) << "Backend & Workload"
              << std::right
              << std::setw(13) << "Mean"
              << std::setw(13) << "p50"
              << std::setw(13) << "p90"
              << std::setw(13) << "p99"
              << std::setw(13) << "Max"
              << std::setw(16) << "Throughput" << "\n";
    std::cout << std::string(103, '-') << "\n";

    // Backend A
    {
        OrderedMapOrderBook bookA(config);
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            bookA.add_order(i, Side::Sell, 1050, 10);
        }

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            OrderId taker_id = BENCH_OPS + i;
            auto start = std::chrono::steady_clock::now();
            bookA.add_order(taker_id, Side::Buy, 1050, 10, TimeInForce::FAK);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend A (std::map Baseline)", stats);
    }

    // Backend B
    {
        OptimizedOrderBook bookB(config);
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            bookB.add_order(i, Side::Sell, 1050, 10);
        }

        std::vector<uint64_t> lats;
        lats.reserve(BENCH_OPS);

        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 1; i <= BENCH_OPS; ++i) {
            OrderId taker_id = BENCH_OPS + i;
            auto start = std::chrono::steady_clock::now();
            bookB.add_order(taker_id, Side::Buy, 1050, 10, TimeInForce::FAK);
            auto end = std::chrono::steady_clock::now();
            lats.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        auto stats = compute_stats(lats, elapsed);
        print_row("Backend B (Atlas Optimized)", stats);
    }
    std::cout << "\n";

    // -------------------------------------------------------------
    // Workload 4: Mixed Realistic HFT Workload (50% Add, 25% Cancel, 25% Modify)
    // -------------------------------------------------------------
    std::cout << ">>> 4. REALISTIC SYNTHETIC REPLAY THROUGHPUT (100k Sequenced Events)\n";
    WorkloadParams wp;
    wp.event_count = 100'000;
    wp.seed = 42;
    auto events = SyntheticGenerator::generate(wp);

    {
        OrderedMapOrderBook bookA(config);
        auto t0 = std::chrono::steady_clock::now();
        for (const auto& ev : events) {
            if (ev.action == Action::Add) {
                bookA.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif);
            } else if (ev.action == Action::Cancel) {
                bookA.cancel_order(ev.order_id);
            } else if (ev.action == Action::Modify) {
                bookA.modify_order(ev.order_id, ev.price, ev.qty);
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double dur = std::chrono::duration<double>(t1 - t0).count();
        double mops = (static_cast<double>(events.size()) / 1'000'000.0) / dur;
        std::cout << "  Backend A Replay Time: " << std::fixed << std::setprecision(3) << dur * 1000.0 << " ms"
                  << " | Throughput: " << std::setprecision(2) << mops << " M events/sec\n";
    }

    {
        OptimizedOrderBook bookB(config);
        auto t0 = std::chrono::steady_clock::now();
        for (const auto& ev : events) {
            if (ev.action == Action::Add) {
                bookB.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif);
            } else if (ev.action == Action::Cancel) {
                bookB.cancel_order(ev.order_id);
            } else if (ev.action == Action::Modify) {
                bookB.modify_order(ev.order_id, ev.price, ev.qty);
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double dur = std::chrono::duration<double>(t1 - t0).count();
        double mops = (static_cast<double>(events.size()) / 1'000'000.0) / dur;
        std::cout << "  Backend B Replay Time: " << std::fixed << std::setprecision(3) << dur * 1000.0 << " ms"
                  << " | Throughput: " << std::setprecision(2) << mops << " M events/sec\n";
    }

    std::cout << "\n========================================================================================\n";
    std::cout << "                           BENCHMARK EXECUTION COMPLETE                                 \n";
    std::cout << "========================================================================================\n";

    return 0;
}
