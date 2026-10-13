#pragma once

#include "book/order_book_interface.hpp"
#include "core/config.hpp"
#include <map>
#include <list>
#include <unordered_map>
#include <algorithm>

namespace atlas::book {

class OrderedMapOrderBook final : public IOrderBook {
public:
    explicit OrderedMapOrderBook(core::EngineConfig config = {})
        : config_(config) {}

    ~OrderedMapOrderBook() override = default;

    core::MatchResult add_order(core::OrderId id, 
                                core::Side side, 
                                core::Price price, 
                                core::Qty qty, 
                                core::TimeInForce tif = core::TimeInForce::GTC, 
                                core::TimestampNs ts = 0) override {
        core::MatchResult result;
        result.order_id = id;
        result.side = side;
        result.price = price;
        result.requested_qty = qty;

        // 1. Validation
        if (id == core::INVALID_ORDER_ID) {
            result.reject_reason = core::RejectReason::OrderNotFound;
            notify_rejected(id, result.reject_reason, ts);
            return result;
        }
        if (price <= 0) {
            result.reject_reason = core::RejectReason::InvalidPrice;
            notify_rejected(id, result.reject_reason, ts);
            return result;
        }
        if (qty == 0) {
            result.reject_reason = core::RejectReason::InvalidQuantity;
            notify_rejected(id, result.reject_reason, ts);
            return result;
        }
        if (order_index_.contains(id)) {
            result.reject_reason = core::RejectReason::DuplicateOrderId;
            notify_rejected(id, result.reject_reason, ts);
            return result;
        }

        result.success = true;
        notify_accepted(id, side, price, qty, tif, ts);

        core::Qty remaining_qty = qty;

        // 2. Matching against opposing side
        if (side == core::Side::Buy) {
            match_buy(id, price, remaining_qty, ts, result);
        } else {
            match_sell(id, price, remaining_qty, ts, result);
        }

        result.executed_qty = qty - remaining_qty;

        // 3. Post-matching handling of remaining quantity
        if (remaining_qty > 0) {
            if (tif == core::TimeInForce::GTC) {
                // Rest in book
                rest_order(id, side, price, remaining_qty, qty, ts);
                result.resting_qty = remaining_qty;
                notify_resting(id, side, price, remaining_qty, ts);
            } else { // FAK
                // Cancel remainder
                result.cancelled_qty = remaining_qty;
                notify_cancelled(id, side, price, remaining_qty, core::RejectReason::None, ts);
            }
        }

        return result;
    }

    core::CancelResult cancel_order(core::OrderId id, core::TimestampNs ts = 0) override {
        core::CancelResult result;
        result.order_id = id;

        auto it = order_index_.find(id);
        if (it == order_index_.end()) {
            result.reject_reason = core::RejectReason::OrderNotFound;
            return result;
        }

        const auto& handle = it->second;
        core::Side side = handle.side;
        core::Price price = handle.price;
        auto queue_it = handle.queue_it;
        core::Qty cancelled_qty = queue_it->qty;

        if (side == core::Side::Buy) {
            auto level_it = bids_.find(price);
            if (level_it != bids_.end()) {
                level_it->second.total_qty -= cancelled_qty;
                level_it->second.order_count--;
                total_bid_volume_ -= cancelled_qty;
                level_it->second.orders.erase(queue_it);
                if (level_it->second.orders.empty()) {
                    bids_.erase(level_it);
                }
            }
        } else {
            auto level_it = asks_.find(price);
            if (level_it != asks_.end()) {
                level_it->second.total_qty -= cancelled_qty;
                level_it->second.order_count--;
                total_ask_volume_ -= cancelled_qty;
                level_it->second.orders.erase(queue_it);
                if (level_it->second.orders.empty()) {
                    asks_.erase(level_it);
                }
            }
        }

        order_index_.erase(it);

        result.success = true;
        result.side = side;
        result.price = price;
        result.cancelled_qty = cancelled_qty;

        notify_cancelled(id, side, price, cancelled_qty, core::RejectReason::None, ts);
        return result;
    }

    core::ModifyResult modify_order(core::OrderId id, 
                                    core::Price new_price, 
                                    core::Qty new_qty, 
                                    core::TimestampNs ts = 0) override {
        core::ModifyResult result;
        result.order_id = id;

        if (new_price <= 0) {
            result.reject_reason = core::RejectReason::InvalidPrice;
            return result;
        }
        if (new_qty == 0) {
            result.reject_reason = core::RejectReason::InvalidQuantity;
            return result;
        }

        auto it = order_index_.find(id);
        if (it == order_index_.end()) {
            result.reject_reason = core::RejectReason::OrderNotFound;
            return result;
        }

        auto& handle = it->second;
        core::Side side = handle.side;
        core::Price old_price = handle.price;
        auto queue_it = handle.queue_it;
        core::Qty old_qty = queue_it->qty;

        result.side = side;
        result.old_price = old_price;
        result.new_price = new_price;
        result.old_qty = old_qty;
        result.new_qty = new_qty;

        // Check if price is same and quantity is decreasing (retains priority under venue policy)
        if (new_price == old_price && new_qty <= old_qty && config_.qty_reduction_retains_priority) {
            core::Qty diff = old_qty - new_qty;
            queue_it->qty = new_qty;
            if (side == core::Side::Buy) {
                bids_[old_price].total_qty -= diff;
                total_bid_volume_ -= diff;
            } else {
                asks_[old_price].total_qty -= diff;
                total_ask_volume_ -= diff;
            }
            result.success = true;
            result.priority_lost = false;
            notify_modified(id, side, old_price, new_price, old_qty, new_qty, false, ts);
            return result;
        }

        // Otherwise: order loses priority (either quantity increased or price changed)
        // First, cancel the resting order
        cancel_order(id, ts);

        // Then re-insert with new price and new quantity
        auto match_res = add_order(id, side, new_price, new_qty, core::TimeInForce::GTC, ts);
        result.success = match_res.success;
        result.priority_lost = true;
        result.trades = std::move(match_res.trades);

        notify_modified(id, side, old_price, new_price, old_qty, new_qty, true, ts);
        return result;
    }

    [[nodiscard]] std::optional<core::Price> get_best_bid() const noexcept override {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    [[nodiscard]] std::optional<core::Price> get_best_ask() const noexcept override {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    [[nodiscard]] std::optional<core::Price> get_spread() const noexcept override {
        auto bb = get_best_bid();
        auto ba = get_best_ask();
        if (bb && ba) return *ba - *bb;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_midpoint() const noexcept override {
        auto bb = get_best_bid();
        auto ba = get_best_ask();
        if (bb && ba) return (static_cast<double>(*bb) + static_cast<double>(*ba)) / 2.0;
        return std::nullopt;
    }

    [[nodiscard]] market_data::MarketDepth get_depth(size_t max_levels = 10) const override {
        market_data::MarketDepth depth;
        depth.seq = seq_;
        
        depth.bids.reserve(std::min(bids_.size(), max_levels));
        size_t count = 0;
        for (const auto& [px, lvl] : bids_) {
            if (count++ >= max_levels) break;
            depth.bids.push_back({px, lvl.total_qty, lvl.order_count});
        }

        depth.asks.reserve(std::min(asks_.size(), max_levels));
        count = 0;
        for (const auto& [px, lvl] : asks_) {
            if (count++ >= max_levels) break;
            depth.asks.push_back({px, lvl.total_qty, lvl.order_count});
        }

        return depth;
    }

    [[nodiscard]] size_t order_count() const noexcept override {
        return order_index_.size();
    }

    [[nodiscard]] core::Qty total_volume() const noexcept override {
        return total_bid_volume_ + total_ask_volume_;
    }

    [[nodiscard]] core::Qty bid_volume() const noexcept override {
        return total_bid_volume_;
    }

    [[nodiscard]] core::Qty ask_volume() const noexcept override {
        return total_ask_volume_;
    }

    [[nodiscard]] bool has_order(core::OrderId id) const noexcept override {
        return order_index_.contains(id);
    }

    [[nodiscard]] std::optional<core::Order> get_order(core::OrderId id) const override {
        auto it = order_index_.find(id);
        if (it == order_index_.end()) return std::nullopt;
        return *(it->second.queue_it);
    }

    [[nodiscard]] core::SeqNum get_seq() const noexcept override {
        return seq_;
    }

    [[nodiscard]] uint64_t compute_state_hash() const override {
        uint64_t hash = FNV_OFFSET_BASIS;
        // Bids: descending price order
        for (const auto& [px, lvl] : bids_) {
            fnv1a_combine(hash, static_cast<uint64_t>(px));
            fnv1a_combine(hash, lvl.total_qty);
            fnv1a_combine(hash, static_cast<uint64_t>(lvl.order_count));
            for (const auto& ord : lvl.orders) {
                fnv1a_combine(hash, ord.id);
                fnv1a_combine(hash, ord.qty);
            }
        }
        // Asks: ascending price order
        for (const auto& [px, lvl] : asks_) {
            fnv1a_combine(hash, static_cast<uint64_t>(px));
            fnv1a_combine(hash, lvl.total_qty);
            fnv1a_combine(hash, static_cast<uint64_t>(lvl.order_count));
            for (const auto& ord : lvl.orders) {
                fnv1a_combine(hash, ord.id);
                fnv1a_combine(hash, ord.qty);
            }
        }
        return hash;
    }

    void clear() override {
        bids_.clear();
        asks_.clear();
        order_index_.clear();
        total_bid_volume_ = 0;
        total_ask_volume_ = 0;
    }

    void set_event_listener(core::EventListener* listener) override {
        listener_ = listener;
    }

    // Direct read-only inspection for invariant testing
    struct LevelInfo {
        core::Price price;
        core::Qty total_qty;
        size_t order_count;
        std::vector<core::Order> orders;
    };

    [[nodiscard]] std::vector<LevelInfo> get_bid_levels() const {
        std::vector<LevelInfo> res;
        for (const auto& [px, lvl] : bids_) {
            LevelInfo info{px, lvl.total_qty, lvl.order_count, {lvl.orders.begin(), lvl.orders.end()}};
            res.push_back(std::move(info));
        }
        return res;
    }

    [[nodiscard]] std::vector<LevelInfo> get_ask_levels() const {
        std::vector<LevelInfo> res;
        for (const auto& [px, lvl] : asks_) {
            LevelInfo info{px, lvl.total_qty, lvl.order_count, {lvl.orders.begin(), lvl.orders.end()}};
            res.push_back(std::move(info));
        }
        return res;
    }

private:
    struct MapPriceLevel {
        core::Price price{0};
        core::Qty total_qty{0};
        size_t order_count{0};
        std::list<core::Order> orders{};
    };

    struct OrderHandle {
        core::Side side;
        core::Price price;
        std::list<core::Order>::iterator queue_it;
    };

    void match_buy(core::OrderId taker_id, core::Price limit_px, core::Qty& remaining_qty, 
                   core::TimestampNs ts, core::MatchResult& result) {
        auto it = asks_.begin();
        while (remaining_qty > 0 && it != asks_.end() && it->first <= limit_px) {
            auto& level = it->second;
            auto order_it = level.orders.begin();

            while (remaining_qty > 0 && order_it != level.orders.end()) {
                auto& maker_order = *order_it;
                core::Qty match_qty = std::min(remaining_qty, maker_order.qty);
                core::Price exec_px = maker_order.price; // Maker's price

                // Trade generation
                core::TradeEvent trade{
                    .seq = ++seq_,
                    .timestamp = ts,
                    .price = exec_px,
                    .qty = match_qty,
                    .maker_order_id = maker_order.id,
                    .taker_order_id = taker_id,
                    .maker_side = core::Side::Sell
                };
                result.trades.push_back(trade);
                if (listener_) listener_->on_trade(trade);

                maker_order.qty -= match_qty;
                level.total_qty -= match_qty;
                total_ask_volume_ -= match_qty;
                remaining_qty -= match_qty;

                if (maker_order.qty == 0) {
                    order_index_.erase(maker_order.id);
                    level.order_count--;
                    order_it = level.orders.erase(order_it);
                } else {
                    break; // Current front order still has resting quantity
                }
            }

            if (level.orders.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void match_sell(core::OrderId taker_id, core::Price limit_px, core::Qty& remaining_qty, 
                    core::TimestampNs ts, core::MatchResult& result) {
        auto it = bids_.begin();
        while (remaining_qty > 0 && it != bids_.end() && it->first >= limit_px) {
            auto& level = it->second;
            auto order_it = level.orders.begin();

            while (remaining_qty > 0 && order_it != level.orders.end()) {
                auto& maker_order = *order_it;
                core::Qty match_qty = std::min(remaining_qty, maker_order.qty);
                core::Price exec_px = maker_order.price; // Maker's price

                core::TradeEvent trade{
                    .seq = ++seq_,
                    .timestamp = ts,
                    .price = exec_px,
                    .qty = match_qty,
                    .maker_order_id = maker_order.id,
                    .taker_order_id = taker_id,
                    .maker_side = core::Side::Buy
                };
                result.trades.push_back(trade);
                if (listener_) listener_->on_trade(trade);

                maker_order.qty -= match_qty;
                level.total_qty -= match_qty;
                total_bid_volume_ -= match_qty;
                remaining_qty -= match_qty;

                if (maker_order.qty == 0) {
                    order_index_.erase(maker_order.id);
                    level.order_count--;
                    order_it = level.orders.erase(order_it);
                } else {
                    break;
                }
            }

            if (level.orders.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void rest_order(core::OrderId id, core::Side side, core::Price price, 
                    core::Qty resting_qty, core::Qty initial_qty, core::TimestampNs ts) {
        core::Order order{
            .id = id,
            .price = price,
            .qty = resting_qty,
            .initial_qty = initial_qty,
            .side = side,
            .tif = core::TimeInForce::GTC,
            .timestamp = ts,
            .seq = ++seq_
        };

        if (side == core::Side::Buy) {
            auto& level = bids_[price];
            level.price = price;
            level.total_qty += resting_qty;
            level.order_count++;
            total_bid_volume_ += resting_qty;
            level.orders.push_back(order);
            auto queue_it = std::prev(level.orders.end());
            order_index_[id] = OrderHandle{side, price, queue_it};
        } else {
            auto& level = asks_[price];
            level.price = price;
            level.total_qty += resting_qty;
            level.order_count++;
            total_ask_volume_ += resting_qty;
            level.orders.push_back(order);
            auto queue_it = std::prev(level.orders.end());
            order_index_[id] = OrderHandle{side, price, queue_it};
        }
    }

    void notify_accepted(core::OrderId id, core::Side side, core::Price price, core::Qty qty, 
                         core::TimeInForce tif, core::TimestampNs ts) {
        if (listener_) {
            listener_->on_order_accepted({++seq_, ts, id, side, price, qty, tif});
        }
    }

    void notify_resting(core::OrderId id, core::Side side, core::Price price, core::Qty qty, core::TimestampNs ts) {
        if (listener_) {
            listener_->on_order_resting({++seq_, ts, id, side, price, qty});
        }
    }

    void notify_cancelled(core::OrderId id, core::Side side, core::Price price, core::Qty qty, 
                          core::RejectReason reason, core::TimestampNs ts) {
        if (listener_) {
            listener_->on_order_cancelled({++seq_, ts, id, side, price, qty, reason});
        }
    }

    void notify_modified(core::OrderId id, core::Side side, core::Price old_price, core::Price new_price,
                         core::Qty old_qty, core::Qty new_qty, bool priority_lost, core::TimestampNs ts) {
        if (listener_) {
            listener_->on_order_modified({++seq_, ts, id, side, old_price, new_price, old_qty, new_qty, priority_lost});
        }
    }

    void notify_rejected(core::OrderId id, core::RejectReason reason, core::TimestampNs ts) {
        if (listener_) {
            listener_->on_order_rejected({++seq_, ts, id, reason});
        }
    }

    core::EngineConfig config_;
    core::SeqNum seq_{0};
    core::EventListener* listener_{nullptr};

    std::map<core::Price, MapPriceLevel, std::greater<core::Price>> bids_;
    std::map<core::Price, MapPriceLevel, std::less<core::Price>> asks_;
    std::unordered_map<core::OrderId, OrderHandle> order_index_;

    core::Qty total_bid_volume_{0};
    core::Qty total_ask_volume_{0};
};

} // namespace atlas::book
