#include <gtest/gtest.h>
#include "book/ordered_map_book.hpp"
#include "book/optimized_book.hpp"

namespace atlas::testing {

using namespace atlas::core;
using namespace atlas::book;

template <typename T>
class OrderBookTypedTest : public ::testing::Test {
protected:
    std::unique_ptr<T> book;

    void SetUp() override {
        book = std::make_unique<T>();
    }
};

using BookImplementations = ::testing::Types<OrderedMapOrderBook, OptimizedOrderBook>;
TYPED_TEST_SUITE(OrderBookTypedTest, BookImplementations);

// 1. Empty book queries
TYPED_TEST(OrderBookTypedTest, EmptyBookBehavior) {
    auto& book = *this->book;
    EXPECT_FALSE(book.get_best_bid().has_value());
    EXPECT_FALSE(book.get_best_ask().has_value());
    EXPECT_FALSE(book.get_spread().has_value());
    EXPECT_FALSE(book.get_midpoint().has_value());
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.total_volume(), 0);
    EXPECT_EQ(book.bid_volume(), 0);
    EXPECT_EQ(book.ask_volume(), 0);

    auto depth = book.get_depth(5);
    EXPECT_TRUE(depth.bids.empty());
    EXPECT_TRUE(depth.asks.empty());
}

// 2. Single order insertion and cancellation
TYPED_TEST(OrderBookTypedTest, SingleOrderInsertionAndCancel) {
    auto& book = *this->book;

    auto res = book.add_order(1, Side::Buy, 100, 50, TimeInForce::GTC);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.resting_qty, 50);
    EXPECT_EQ(res.executed_qty, 0);
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.bid_volume(), 50);
    EXPECT_EQ(book.get_best_bid(), 100);
    EXPECT_FALSE(book.get_best_ask().has_value());

    // Cancel order
    auto cancel_res = book.cancel_order(1);
    EXPECT_TRUE(cancel_res.success);
    EXPECT_EQ(cancel_res.cancelled_qty, 50);
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.bid_volume(), 0);
    EXPECT_FALSE(book.get_best_bid().has_value());
}

// 3. Validation and rejection
TYPED_TEST(OrderBookTypedTest, ValidationAndRejections) {
    auto& book = *this->book;

    // Zero qty
    auto res_zero_qty = book.add_order(1, Side::Buy, 100, 0);
    EXPECT_FALSE(res_zero_qty.success);
    EXPECT_EQ(res_zero_qty.reject_reason, RejectReason::InvalidQuantity);

    // Negative / zero price
    auto res_zero_px = book.add_order(2, Side::Buy, 0, 10);
    EXPECT_FALSE(res_zero_px.success);
    EXPECT_EQ(res_zero_px.reject_reason, RejectReason::InvalidPrice);

    auto res_neg_px = book.add_order(3, Side::Buy, -50, 10);
    EXPECT_FALSE(res_neg_px.success);
    EXPECT_EQ(res_neg_px.reject_reason, RejectReason::InvalidPrice);

    // Add valid order
    auto res_valid = book.add_order(100, Side::Buy, 100, 10);
    EXPECT_TRUE(res_valid.success);

    // Duplicate order ID
    auto res_dup = book.add_order(100, Side::Sell, 105, 20);
    EXPECT_FALSE(res_dup.success);
    EXPECT_EQ(res_dup.reject_reason, RejectReason::DuplicateOrderId);

    // Cancel missing order
    auto cancel_missing = book.cancel_order(999);
    EXPECT_FALSE(cancel_missing.success);
    EXPECT_EQ(cancel_missing.reject_reason, RejectReason::OrderNotFound);
}

// 4. Spread and Midpoint calculation
TYPED_TEST(OrderBookTypedTest, SpreadAndMidpoint) {
    auto& book = *this->book;

    book.add_order(1, Side::Buy, 100, 10);
    book.add_order(2, Side::Sell, 104, 20);

    EXPECT_EQ(book.get_best_bid(), 100);
    EXPECT_EQ(book.get_best_ask(), 104);
    EXPECT_EQ(book.get_spread(), 4);
    EXPECT_DOUBLE_EQ(*book.get_midpoint(), 102.0);

    // Add better bid
    book.add_order(3, Side::Buy, 102, 15);
    EXPECT_EQ(book.get_best_bid(), 102);
    EXPECT_EQ(book.get_spread(), 2);
    EXPECT_DOUBLE_EQ(*book.get_midpoint(), 103.0);
}

// 5. Basic Buy and Sell matching with maker pricing
TYPED_TEST(OrderBookTypedTest, BasicMatchingMakerPricing) {
    auto& book = *this->book;

    // Maker rests Ask at 105
    book.add_order(1, Side::Sell, 105, 100);

    // Aggressive Buyer bids 110 (willing to pay up to 110)
    auto match_res = book.add_order(2, Side::Buy, 110, 40);
    EXPECT_TRUE(match_res.success);
    EXPECT_EQ(match_res.executed_qty, 40);
    EXPECT_EQ(match_res.resting_qty, 0);
    ASSERT_EQ(match_res.trades.size(), 1);

    const auto& trade = match_res.trades[0];
    EXPECT_EQ(trade.price, 105); // Must execute at maker's price (105), NOT taker's price (110)
    EXPECT_EQ(trade.qty, 40);
    EXPECT_EQ(trade.maker_order_id, 1);
    EXPECT_EQ(trade.taker_order_id, 2);

    // Resting maker should now have 60 left
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.ask_volume(), 60);
    EXPECT_EQ(book.get_best_ask(), 105);
}

// 6. Price-Time Priority (FIFO)
TYPED_TEST(OrderBookTypedTest, PriceTimePriorityFIFO) {
    auto& book = *this->book;

    // Resting orders at same price 100
    book.add_order(1, Side::Buy, 100, 30); // Arrived 1st
    book.add_order(2, Side::Buy, 100, 50); // Arrived 2nd
    book.add_order(3, Side::Buy, 100, 20); // Arrived 3rd

    // Aggressive Sell for 60 @ 100
    auto match_res = book.add_order(10, Side::Sell, 100, 60);
    EXPECT_TRUE(match_res.success);
    EXPECT_EQ(match_res.executed_qty, 60);
    ASSERT_EQ(match_res.trades.size(), 2);

    // 1st trade: Order 1 fully filled (30)
    EXPECT_EQ(match_res.trades[0].maker_order_id, 1);
    EXPECT_EQ(match_res.trades[0].qty, 30);

    // 2nd trade: Order 2 partially filled (30 out of 50)
    EXPECT_EQ(match_res.trades[1].maker_order_id, 2);
    EXPECT_EQ(match_res.trades[1].qty, 30);

    // Book state: Order 2 has 20 remaining, Order 3 has 20 remaining
    EXPECT_EQ(book.order_count(), 2);
    EXPECT_EQ(book.bid_volume(), 40);

    // Check that Order 2 is still ahead of Order 3 by sending another sell for 20
    auto match2 = book.add_order(11, Side::Sell, 100, 20);
    ASSERT_EQ(match2.trades.size(), 1);
    EXPECT_EQ(match2.trades[0].maker_order_id, 2);
    EXPECT_EQ(match2.trades[0].qty, 20);

    // Order 3 remains
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_TRUE(book.has_order(3));
    EXPECT_FALSE(book.has_order(2));
}

// 7. Multiple price level execution (walking the book)
TYPED_TEST(OrderBookTypedTest, MultiLevelMatching) {
    auto& book = *this->book;

    book.add_order(1, Side::Sell, 101, 10);
    book.add_order(2, Side::Sell, 102, 20);
    book.add_order(3, Side::Sell, 103, 30);

    // Buyer sweeps book up to 103 for total 45 shares
    auto match_res = book.add_order(10, Side::Buy, 103, 45);
    EXPECT_TRUE(match_res.success);
    EXPECT_EQ(match_res.executed_qty, 45);
    EXPECT_EQ(match_res.resting_qty, 0);
    ASSERT_EQ(match_res.trades.size(), 3);

    EXPECT_EQ(match_res.trades[0].price, 101);
    EXPECT_EQ(match_res.trades[0].qty, 10);

    EXPECT_EQ(match_res.trades[1].price, 102);
    EXPECT_EQ(match_res.trades[1].qty, 20);

    EXPECT_EQ(match_res.trades[2].price, 103);
    EXPECT_EQ(match_res.trades[2].qty, 15);

    // Level 101 and 102 should be completely removed, level 103 has 15 remaining
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.get_best_ask(), 103);
    EXPECT_EQ(book.ask_volume(), 15);
}

// 8. GTC Resting behavior
TYPED_TEST(OrderBookTypedTest, GTCRestingBehavior) {
    auto& book = *this->book;

    book.add_order(1, Side::Sell, 100, 10);

    // Buy 25 @ 100 GTC: 10 fills against Order 1, remainder 15 rests at 100 Bid
    auto match_res = book.add_order(2, Side::Buy, 100, 25, TimeInForce::GTC);
    EXPECT_TRUE(match_res.success);
    EXPECT_EQ(match_res.executed_qty, 10);
    EXPECT_EQ(match_res.resting_qty, 15);
    EXPECT_EQ(match_res.cancelled_qty, 0);

    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.get_best_bid(), 100);
    EXPECT_FALSE(book.get_best_ask().has_value());
    EXPECT_EQ(book.bid_volume(), 15);
}

// 9. FAK Execution (Fill-And-Kill)
TYPED_TEST(OrderBookTypedTest, FAKBehavior) {
    auto& book = *this->book;

    book.add_order(1, Side::Sell, 100, 20);

    // FAK Buy for 50 @ 100: fills 20, 30 cancelled, 0 rests
    auto match_res = book.add_order(2, Side::Buy, 100, 50, TimeInForce::FAK);
    EXPECT_TRUE(match_res.success);
    EXPECT_EQ(match_res.executed_qty, 20);
    EXPECT_EQ(match_res.resting_qty, 0);
    EXPECT_EQ(match_res.cancelled_qty, 30);

    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.total_volume(), 0);

    // FAK with no matching liquidity
    auto match_nofill = book.add_order(3, Side::Buy, 95, 10, TimeInForce::FAK);
    EXPECT_TRUE(match_nofill.success);
    EXPECT_EQ(match_nofill.executed_qty, 0);
    EXPECT_EQ(match_nofill.resting_qty, 0);
    EXPECT_EQ(match_nofill.cancelled_qty, 10);
    EXPECT_EQ(book.order_count(), 0);
}

// 10. Order Modification rules and priority
TYPED_TEST(OrderBookTypedTest, OrderModificationPriority) {
    auto& book = *this->book;

    // Resting bids: 1 then 2 at price 100
    book.add_order(1, Side::Buy, 100, 50);
    book.add_order(2, Side::Buy, 100, 50);

    // Quantity reduction retains priority
    auto mod_reduce = book.modify_order(1, 100, 30);
    EXPECT_TRUE(mod_reduce.success);
    EXPECT_FALSE(mod_reduce.priority_lost);
    EXPECT_EQ(book.bid_volume(), 80);

    // Match with 35: Order 1 should fill completely (30), and Order 2 fills 5
    auto match1 = book.add_order(10, Side::Sell, 100, 35);
    ASSERT_EQ(match1.trades.size(), 2);
    EXPECT_EQ(match1.trades[0].maker_order_id, 1);
    EXPECT_EQ(match1.trades[0].qty, 30);
    EXPECT_EQ(match1.trades[1].maker_order_id, 2);
    EXPECT_EQ(match1.trades[1].qty, 5);

    // Now Order 2 has 45 remaining. Add Order 3 for 20 at price 100.
    book.add_order(3, Side::Buy, 100, 20);

    // Quantity increase on Order 2 (from 45 to 60) -> MUST LOSE PRIORITY!
    auto mod_inc = book.modify_order(2, 100, 60);
    EXPECT_TRUE(mod_inc.success);
    EXPECT_TRUE(mod_inc.priority_lost);

    // Order 3 should now execute before Order 2!
    auto match2 = book.add_order(11, Side::Sell, 100, 20);
    ASSERT_EQ(match2.trades.size(), 1);
    EXPECT_EQ(match2.trades[0].maker_order_id, 3); // Order 3 executes first!
    EXPECT_EQ(match2.trades[0].qty, 20);

    // Price change: modify Order 2 to 101
    auto mod_px = book.modify_order(2, 101, 60);
    EXPECT_TRUE(mod_px.success);
    EXPECT_TRUE(mod_px.priority_lost);
    EXPECT_EQ(book.get_best_bid(), 101);
}

// 11. Market depth and aggregations
TYPED_TEST(OrderBookTypedTest, MarketDepthAggregation) {
    auto& book = *this->book;

    book.add_order(1, Side::Buy, 100, 10);
    book.add_order(2, Side::Buy, 100, 15);
    book.add_order(3, Side::Buy, 99, 40);
    book.add_order(4, Side::Buy, 98, 50);

    book.add_order(5, Side::Sell, 102, 25);
    book.add_order(6, Side::Sell, 103, 35);
    book.add_order(7, Side::Sell, 103, 10);

    auto depth = book.get_depth(2);
    ASSERT_EQ(depth.bids.size(), 2);
    EXPECT_EQ(depth.bids[0].price, 100);
    EXPECT_EQ(depth.bids[0].total_qty, 25);
    EXPECT_EQ(depth.bids[0].order_count, 2);
    EXPECT_EQ(depth.bids[1].price, 99);
    EXPECT_EQ(depth.bids[1].total_qty, 40);

    ASSERT_EQ(depth.asks.size(), 2);
    EXPECT_EQ(depth.asks[0].price, 102);
    EXPECT_EQ(depth.asks[0].total_qty, 25);
    EXPECT_EQ(depth.asks[1].price, 103);
    EXPECT_EQ(depth.asks[1].total_qty, 45);
    EXPECT_EQ(depth.asks[1].order_count, 2);
}

} // namespace atlas::testing
