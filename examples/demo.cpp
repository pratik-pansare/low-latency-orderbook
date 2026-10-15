#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

#include "book/optimized_book.hpp"
#include "market_data/spsc_ring_buffer.hpp"

using namespace atlas::core;
using namespace atlas::book;
using namespace atlas::market_data;

void print_header(std::string_view title) {
    std::cout << "\n\033[1;36m======================================================================\033[0m\n";
    std::cout << "\033[1;37m " << title << "\033[0m\n";
    std::cout << "\033[1;36m======================================================================\033[0m\n";
}

int main() {
    print_header("ATLAS LOB: HIGH-PERFORMANCE MATCHING ENGINE DEMO");

    EngineConfig config;
    config.max_orders = 100'000;
    config.max_price_levels = 5'000;
    config.qty_reduction_retains_priority = true;

    OptimizedOrderBook book(config);

    // Trade and event listener displaying real-time executions
    class ConsoleListener : public EventListener {
    public:
        void on_trade(const TradeEvent& t) override {
            std::cout << "  \033[1;32m[TRADE]\033[0m Executed " << t.qty << " @ $" 
                      << std::fixed << std::setprecision(2) << (t.price / 100.0)
                      << " | Maker: #" << t.maker_order_id << " (" << (t.maker_side == Side::Buy ? "BUY" : "SELL")
                      << ") vs Taker: #" << t.taker_order_id << "\n";
        }
        void on_order_resting(const OrderRestingEvent& r) override {
            std::cout << "  \033[1;34m[RESTING]\033[0m Order #" << r.order_id << " (" 
                      << (r.side == Side::Buy ? "BUY" : "SELL") << ") resting " 
                      << r.resting_qty << " @ $" << (r.price / 100.0) << "\n";
        }
        void on_order_cancelled(const OrderCancelledEvent& c) override {
            std::cout << "  \033[1;31m[CANCELLED]\033[0m Order #" << c.order_id 
                      << " cancelled qty: " << c.cancelled_qty << "\n";
        }
        void on_order_modified(const OrderModifiedEvent& m) override {
            std::cout << "  \033[1;35m[MODIFIED]\033[0m Order #" << m.order_id << ": " 
                      << m.old_qty << " @ $" << (m.old_price / 100.0) << " -> "
                      << m.new_qty << " @ $" << (m.new_price / 100.0) 
                      << (m.priority_lost ? " (Priority Lost)" : " (Priority Retained)") << "\n";
        }
    } listener;

    book.set_event_listener(&listener);

    // 1. Seeding Liquidity
    std::cout << "\n>>> Phase 1: Seeding Initial Book Liquidity\n";
    book.add_order(101, Side::Buy, 9995, 50);
    book.add_order(102, Side::Buy, 9998, 100);
    book.add_order(103, Side::Buy, 9998, 150);
    book.add_order(104, Side::Buy, 10000, 200); // Best Bid

    book.add_order(201, Side::Sell, 10005, 120); // Best Ask
    book.add_order(202, Side::Sell, 10008, 180);
    book.add_order(203, Side::Sell, 10010, 250);
    book.add_order(204, Side::Sell, 10015, 500);

    std::cout << "\n" << book.get_depth(5).format();
    std::cout << "Spread: $" << std::fixed << std::setprecision(2) << (*book.get_spread() / 100.0)
              << " | Midpoint: $" << (*book.get_midpoint() / 100.0) << "\n";

    // 2. Priority Retention on Quantity Reduction
    std::cout << "\n>>> Phase 2: Order Modification & Priority Policy\n";
    std::cout << "Reducing quantity of Order #102 at $99.98 from 100 to 60:\n";
    book.modify_order(102, 9998, 60);

    std::cout << "\nIncreasing quantity of Order #103 at $99.98 from 150 to 200 (Loses Priority):\n";
    book.modify_order(103, 9998, 200);

    // 3. Aggressive Order Sweeping Book (Multiple Fills)
    std::cout << "\n>>> Phase 3: Aggressive Market Order Sweeping Ask Levels (Walking the Book)\n";
    std::cout << "Incoming Taker Buy for 350 shares @ $100.12 limit:\n";
    auto match_res = book.add_order(301, Side::Buy, 10012, 350, TimeInForce::GTC);
    std::cout << "  Executed Qty: " << match_res.executed_qty 
              << " | Resting Qty: " << match_res.resting_qty << "\n";

    std::cout << "\nBook Depth after sweep:\n" << book.get_depth(5).format();

    // 4. Fill-And-Kill (FAK) Order
    std::cout << "\n>>> Phase 4: Fill-And-Kill (FAK) Order Execution\n";
    std::cout << "Incoming FAK Sell for 250 shares @ $99.96 (Available at $100.00 is 200):\n";
    auto fak_res = book.add_order(401, Side::Sell, 9996, 250, TimeInForce::FAK);
    std::cout << "  FAK Executed: " << fak_res.executed_qty 
              << " | FAK Cancelled Remainder: " << fak_res.cancelled_qty << "\n";

    std::cout << "\nBook Depth after FAK:\n" << book.get_depth(5).format();

    // 5. State Checksum Verification
    std::cout << "\n>>> Phase 5: Deterministic Checksum Verification\n";
    uint64_t hash = book.compute_state_hash();
    std::cout << "  64-bit FNV-1a Book State Checksum: 0x" << std::hex << hash << std::dec << "\n";
    std::cout << "  Total Resting Orders: " << book.order_count() 
              << " | Total Resting Volume: " << book.total_volume() << " shares\n";

    // 6. Asynchronous SPSC Ring Buffer Demo
    std::cout << "\n>>> Phase 6: Lock-Free SPSC Ring Buffer Event Publishing\n";
    SpscRingBuffer<TradeEvent, 1024> event_queue;
    std::atomic<bool> consumer_running{true};
    size_t consumed_count = 0;

    // Background consumer thread
    std::thread consumer([&]() {
        TradeEvent t;
        while (consumer_running.load(std::memory_order_relaxed) || !event_queue.empty()) {
            if (event_queue.try_pop(t)) {
                consumed_count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Producer publishing 10,000 trades
    for (uint64_t i = 1; i <= 10'000; ++i) {
        TradeEvent t{.seq = i, .price = 10000, .qty = 10, .maker_order_id = i, .taker_order_id = 99999};
        while (!event_queue.try_push(t)) {
            std::this_thread::yield();
        }
    }

    consumer_running.store(false, std::memory_order_release);
    consumer.join();

    std::cout << "  Successfully transferred " << consumed_count 
              << " trade events across threads via cache-aligned SPSC ring buffer.\n";

    print_header("DEMONSTRATION COMPLETED SUCCESSFULLY");
    return 0;
}
