#include <gtest/gtest.h>
#include "book/ordered_map_book.hpp"
#include "book/optimized_book.hpp"
#include "tests/test_invariants.hpp"
#include <random>
#include <vector>
#include <iostream>
#include <fstream>

namespace atlas::testing {

using namespace atlas::core;
using namespace atlas::book;

TEST(DifferentialTest, RandomizedDualBackendComparison) {
    constexpr uint64_t SEED = 42424242;
    std::mt19937_64 rng(SEED);

    EngineConfig config;
    config.max_orders = 50'000;
    config.max_price_levels = 10'000;
    config.qty_reduction_retains_priority = true;

    OrderedMapOrderBook bookA(config);
    OptimizedOrderBook bookB(config);

    std::uniform_int_distribution<int> op_dist(0, 9); // 0-5 Add, 6-7 Cancel, 8-9 Modify
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> tif_dist(0, 9); // 80% GTC, 20% FAK
    std::normal_distribution<double> price_dist(1000.0, 15.0); // Mean $10.00, stddev $0.15
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

    std::vector<OrderId> live_orders;
    OrderId next_order_id = 1;

    constexpr size_t NUM_OPERATIONS = 10'000;

    for (size_t op_idx = 0; op_idx < NUM_OPERATIONS; ++op_idx) {
        int op = op_dist(rng);

        if (op <= 5 || live_orders.empty()) {
            // ADD ORDER
            OrderId id = next_order_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            Price px = std::max<Price>(1, static_cast<Price>(std::round(price_dist(rng))));
            Qty qty = qty_dist(rng);
            TimeInForce tif = (tif_dist(rng) < 8) ? TimeInForce::GTC : TimeInForce::FAK;

            auto resA = bookA.add_order(id, side, px, qty, tif);
            auto resB = bookB.add_order(id, side, px, qty, tif);

            ASSERT_EQ(resA.success, resB.success) << "Seed=" << SEED << ", op=" << op_idx;
            ASSERT_EQ(resA.reject_reason, resB.reject_reason);
            ASSERT_EQ(resA.executed_qty, resB.executed_qty);
            ASSERT_EQ(resA.resting_qty, resB.resting_qty);
            ASSERT_EQ(resA.cancelled_qty, resB.cancelled_qty);
            ASSERT_EQ(resA.trades.size(), resB.trades.size());

            for (size_t t = 0; t < resA.trades.size(); ++t) {
                ASSERT_EQ(resA.trades[t].price, resB.trades[t].price);
                ASSERT_EQ(resA.trades[t].qty, resB.trades[t].qty);
                ASSERT_EQ(resA.trades[t].maker_order_id, resB.trades[t].maker_order_id);
                ASSERT_EQ(resA.trades[t].taker_order_id, resB.trades[t].taker_order_id);
            }

            if (resA.resting_qty > 0) {
                live_orders.push_back(id);
            }
        } else if (op <= 7) {
            // CANCEL ORDER
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t sel_idx = idx_dist(rng);
            OrderId target_id = live_orders[sel_idx];

            auto canA = bookA.cancel_order(target_id);
            auto canB = bookB.cancel_order(target_id);

            ASSERT_EQ(canA.success, canB.success);
            ASSERT_EQ(canA.cancelled_qty, canB.cancelled_qty);

            live_orders[sel_idx] = live_orders.back();
            live_orders.pop_back();
        } else {
            // MODIFY ORDER
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t sel_idx = idx_dist(rng);
            OrderId target_id = live_orders[sel_idx];

            auto cur_ord = bookA.get_order(target_id);
            if (cur_ord) {
                std::uniform_int_distribution<int> mod_type(0, 2);
                Price new_px = cur_ord->price;
                Qty new_qty = cur_ord->qty;

                int mtype = mod_type(rng);
                if (mtype == 0) {
                    // Reduce qty (retains priority)
                    new_qty = std::max<Qty>(1, cur_ord->qty / 2);
                } else if (mtype == 1) {
                    // Increase qty (loses priority)
                    new_qty = cur_ord->qty + qty_dist(rng);
                } else {
                    // Change price
                    new_px = std::max<Price>(1, static_cast<Price>(std::round(price_dist(rng))));
                }

                auto modA = bookA.modify_order(target_id, new_px, new_qty);
                auto modB = bookB.modify_order(target_id, new_px, new_qty);

                ASSERT_EQ(modA.success, modB.success);
                ASSERT_EQ(modA.priority_lost, modB.priority_lost);
                ASSERT_EQ(modA.old_qty, modB.old_qty);
                ASSERT_EQ(modA.new_qty, modB.new_qty);
                ASSERT_EQ(modA.trades.size(), modB.trades.size());

                if (!modA.success || !bookA.has_order(target_id)) {
                    live_orders[sel_idx] = live_orders.back();
                    live_orders.pop_back();
                }
            }
        }

        // Periodic state and invariant verification
        if (op_idx % 100 == 0) {
            verify_invariants(bookA);
            verify_invariants(bookB);

            ASSERT_EQ(bookA.order_count(), bookB.order_count());
            ASSERT_EQ(bookA.total_volume(), bookB.total_volume());
            ASSERT_EQ(bookA.bid_volume(), bookB.bid_volume());
            ASSERT_EQ(bookA.ask_volume(), bookB.ask_volume());
            ASSERT_EQ(bookA.get_best_bid(), bookB.get_best_bid());
            ASSERT_EQ(bookA.get_best_ask(), bookB.get_best_ask());

            // 64-bit deterministic state checksum verification!
            ASSERT_EQ(bookA.compute_state_hash(), bookB.compute_state_hash())
                << "State hash mismatch at op " << op_idx;
        }
    }

    // Final verification
    verify_invariants(bookA);
    verify_invariants(bookB);
    ASSERT_EQ(bookA.compute_state_hash(), bookB.compute_state_hash());
}

} // namespace atlas::testing
