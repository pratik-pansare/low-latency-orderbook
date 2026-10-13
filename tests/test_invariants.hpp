#pragma once

#include "book/order_book_interface.hpp"
#include <gtest/gtest.h>

namespace atlas::testing {

inline void verify_invariants(const book::IOrderBook& book) {
    auto best_bid = book.get_best_bid();
    auto best_ask = book.get_best_ask();

    // 1. Non-crossing invariant
    if (best_bid && best_ask) {
        EXPECT_LT(*best_bid, *best_ask)
            << "Invariant violation: crossed book (best_bid=" << *best_bid 
            << " >= best_ask=" << *best_ask << ")";
    }

    // 2. Spread consistency
    auto spread = book.get_spread();
    if (best_bid && best_ask) {
        ASSERT_TRUE(spread.has_value());
        EXPECT_EQ(*spread, *best_ask - *best_bid);
    } else {
        EXPECT_FALSE(spread.has_value());
    }

    // 3. Midpoint consistency
    auto midpoint = book.get_midpoint();
    if (best_bid && best_ask) {
        ASSERT_TRUE(midpoint.has_value());
        EXPECT_DOUBLE_EQ(*midpoint, (*best_bid + *best_ask) / 2.0);
    } else {
        EXPECT_FALSE(midpoint.has_value());
    }

    // 4. Volume aggregation consistency
    EXPECT_EQ(book.total_volume(), book.bid_volume() + book.ask_volume())
        << "Invariant violation: total volume != bid_volume + ask_volume";

    // 5. Depth query consistency
    auto depth = book.get_depth(20);
    if (best_bid) {
        ASSERT_FALSE(depth.bids.empty());
        EXPECT_EQ(depth.bids.front().price, *best_bid);
    } else {
        EXPECT_TRUE(depth.bids.empty());
    }

    if (best_ask) {
        ASSERT_FALSE(depth.asks.empty());
        EXPECT_EQ(depth.asks.front().price, *best_ask);
    } else {
        EXPECT_TRUE(depth.asks.empty());
    }

    // Verify bids are strictly descending and non-zero qty
    for (size_t i = 1; i < depth.bids.size(); ++i) {
        EXPECT_GT(depth.bids[i - 1].price, depth.bids[i].price);
        EXPECT_GT(depth.bids[i].total_qty, 0);
        EXPECT_GT(depth.bids[i].order_count, 0);
    }

    // Verify asks are strictly ascending and non-zero qty
    for (size_t i = 1; i < depth.asks.size(); ++i) {
        EXPECT_LT(depth.asks[i - 1].price, depth.asks[i].price);
        EXPECT_GT(depth.asks[i].total_qty, 0);
        EXPECT_GT(depth.asks[i].order_count, 0);
    }
}

} // namespace atlas::testing
