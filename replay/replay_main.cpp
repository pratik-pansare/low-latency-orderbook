#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>

#include "book/ordered_map_book.hpp"
#include "book/optimized_book.hpp"
#include "replay/synthetic_generator.hpp"

using namespace atlas::core;
using namespace atlas::book;
using namespace atlas::replay;

void print_usage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " --generate <file.csv> [--count N] [--seed S]\n"
              << "  " << prog << " --replay <file.csv> [--backend ordered_map|optimized] [--snapshot N]\n"
              << "  " << prog << " --compare <file.csv>\n"
              << "  " << prog << " --benchmark-replay [--count N]\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        std::cout << "\nRunning default self-contained replay demo with 25,000 synthetic events...\n";

        WorkloadParams params;
        params.event_count = 25'000;
        params.seed = 42;
        auto events = SyntheticGenerator::generate(params);

        EngineConfig cfg;
        cfg.max_orders = 100'000;
        cfg.max_price_levels = 10'000;

        OrderedMapOrderBook bookA(cfg);
        OptimizedOrderBook bookB(cfg);

        class TradeCounter : public EventListener {
        public:
            size_t count{0};
            void on_trade(const TradeEvent&) override { count++; }
        } listenerA, listenerB;

        bookA.set_event_listener(&listenerA);
        bookB.set_event_listener(&listenerB);

        auto t0 = std::chrono::steady_clock::now();
        for (const auto& ev : events) {
            if (ev.action == Action::Add) bookA.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif, ev.timestamp);
            else if (ev.action == Action::Cancel) bookA.cancel_order(ev.order_id, ev.timestamp);
            else if (ev.action == Action::Modify) bookA.modify_order(ev.order_id, ev.price, ev.qty, ev.timestamp);
        }
        auto t1 = std::chrono::steady_clock::now();

        auto t2 = std::chrono::steady_clock::now();
        for (const auto& ev : events) {
            if (ev.action == Action::Add) bookB.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif, ev.timestamp);
            else if (ev.action == Action::Cancel) bookB.cancel_order(ev.order_id, ev.timestamp);
            else if (ev.action == Action::Modify) bookB.modify_order(ev.order_id, ev.price, ev.qty, ev.timestamp);
        }
        auto t3 = std::chrono::steady_clock::now();

        double durA = std::chrono::duration<double>(t1 - t0).count();
        double durB = std::chrono::duration<double>(t3 - t2).count();

        std::cout << "\nReplay Results (" << events.size() << " events):\n";
        std::cout << "  Backend A (std::map):  " << durA * 1000.0 << " ms (" 
                  << (events.size() / durA) / 1e6 << " M events/s), Trades: " << listenerA.count 
                  << ", State Hash: 0x" << std::hex << bookA.compute_state_hash() << std::dec << "\n";
        std::cout << "  Backend B (Optimized): " << durB * 1000.0 << " ms (" 
                  << (events.size() / durB) / 1e6 << " M events/s), Trades: " << listenerB.count 
                  << ", State Hash: 0x" << std::hex << bookB.compute_state_hash() << std::dec << "\n";

        if (bookA.compute_state_hash() == bookB.compute_state_hash()) {
            std::cout << "\nSUCCESS: Bit-exact 64-bit state checksum match across both backends!\n";
        } else {
            std::cerr << "\nERROR: Checksum divergence detected!\n";
            return 1;
        }

        std::cout << "\nFinal Book Snapshot:\n" << bookB.get_depth(5).format() << "\n";
        return 0;
    }

    std::string mode = argv[1];

    if (mode == "--generate" && argc >= 3) {
        std::string filename = argv[2];
        size_t count = 50'000;
        uint64_t seed = 42;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--count" && i + 1 < argc) count = std::stoull(argv[++i]);
            if (std::string(argv[i]) == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        }
        WorkloadParams p;
        p.event_count = count;
        p.seed = seed;
        auto events = SyntheticGenerator::generate(p);
        if (SyntheticGenerator::write_to_file(filename, events)) {
            std::cout << "Successfully generated " << events.size() << " events to " << filename << "\n";
        } else {
            std::cerr << "Failed to write to " << filename << "\n";
            return 1;
        }
    } else if (mode == "--compare" && argc >= 3) {
        std::string filename = argv[2];
        auto events = SyntheticGenerator::read_from_file(filename);
        if (events.empty()) {
            std::cerr << "No events read from " << filename << "\n";
            return 1;
        }

        OrderedMapOrderBook bookA;
        OptimizedOrderBook bookB;

        for (const auto& ev : events) {
            if (ev.action == Action::Add) {
                bookA.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif, ev.timestamp);
                bookB.add_order(ev.order_id, ev.side, ev.price, ev.qty, ev.tif, ev.timestamp);
            } else if (ev.action == Action::Cancel) {
                bookA.cancel_order(ev.order_id, ev.timestamp);
                bookB.cancel_order(ev.order_id, ev.timestamp);
            } else if (ev.action == Action::Modify) {
                bookA.modify_order(ev.order_id, ev.price, ev.qty, ev.timestamp);
                bookB.modify_order(ev.order_id, ev.price, ev.qty, ev.timestamp);
            }
        }

        uint64_t hashA = bookA.compute_state_hash();
        uint64_t hashB = bookB.compute_state_hash();

        std::cout << "Replay completed for " << events.size() << " events.\n";
        std::cout << "Backend A Checksum: 0x" << std::hex << hashA << "\n";
        std::cout << "Backend B Checksum: 0x" << std::hex << hashB << std::dec << "\n";

        if (hashA == hashB) {
            std::cout << "MATCH: Deterministic replay verified with bit-exact state parity!\n";
        } else {
            std::cerr << "MISMATCH: Checksums differ!\n";
            return 1;
        }
    } else {
        print_usage(argv[0]);
    }

    return 0;
}
