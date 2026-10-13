#pragma once

#include "book/order_book_interface.hpp"
#include "core/config.hpp"
#include "memory/fixed_pool.hpp"
#include "memory/intrusive_list.hpp"
#include <unordered_map>
#include <algorithm>

namespace atlas::book {

class OptimizedOrderBook final : public IOrderBook {
public:
    struct PriceLevel;

    struct OrderNode {
        core::OrderId id{core::INVALID_ORDER_ID};
        core::Price price{0};
        core::Qty qty{0};
        core::Qty initial_qty{0};
        core::Side side{core::Side::Buy};
        core::TimeInForce tif{core::TimeInForce::GTC};
        core::TimestampNs timestamp{0};
        core::SeqNum seq{0};

        OrderNode* prev{nullptr};
        OrderNode* next{nullptr};
        PriceLevel* level{nullptr};
        memory::GenerationalHandle handle{};
    };

    struct PriceLevel {
        core::Price price{0};
        core::Side side{core::Side::Buy};
        core::Qty total_qty{0};
        size_t order_count{0};
        memory::IntrusiveList<OrderNode> queue{};

        PriceLevel* prev_level{nullptr};
        PriceLevel* next_level{nullptr};
        memory::GenerationalHandle handle{};
    };

    explicit OptimizedOrderBook(core::EngineConfig config = {})
        : config_(config),
          order_pool_(config.max_orders),
          level_pool_(config.max_price_levels) {
        order_index_.reserve(config.max_orders);
        level_index_.reserve(config.max_price_levels);
    }

    ~OptimizedOrderBook() override = default;

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
        if (order_pool_.full()) {
            result.reject_reason = core::RejectReason::BookFull;
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

        OrderNode* node = order_pool_.get(it->second);
        if (!node) {
            order_index_.erase(it);
            result.reject_reason = core::RejectReason::OrderNotFound;
            return result;
        }

        core::Side side = node->side;
        core::Price price = node->price;
        core::Qty cancelled_qty = node->qty;
        PriceLevel* level = node->level;

        level->queue.unlink(node);
        level->total_qty -= cancelled_qty;
        level->order_count--;

        if (side == core::Side::Buy) {
            total_bid_volume_ -= cancelled_qty;
        } else {
            total_ask_volume_ -= cancelled_qty;
        }

        if (level->order_count == 0) {
            remove_price_level(level);
        }

        order_index_.erase(it);
        order_pool_.deallocate(node);

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

        OrderNode* node = order_pool_.get(it->second);
        if (!node) {
            result.reject_reason = core::RejectReason::OrderNotFound;
            return result;
        }

        core::Side side = node->side;
        core::Price old_price = node->price;
        core::Qty old_qty = node->qty;

        result.side = side;
        result.old_price = old_price;
        result.new_price = new_price;
        result.old_qty = old_qty;
        result.new_qty = new_qty;

        // In-place quantity reduction retention
        if (new_price == old_price && new_qty <= old_qty && config_.qty_reduction_retains_priority) {
            core::Qty diff = old_qty - new_qty;
            node->qty = new_qty;
            node->level->total_qty -= diff;
            if (side == core::Side::Buy) {
                total_bid_volume_ -= diff;
            } else {
                total_ask_volume_ -= diff;
            }
            result.success = true;
            result.priority_lost = false;
            notify_modified(id, side, old_price, new_price, old_qty, new_qty, false, ts);
            return result;
        }

        // Order loses priority
        cancel_order(id, ts);
        auto match_res = add_order(id, side, new_price, new_qty, core::TimeInForce::GTC, ts);
        result.success = match_res.success;
        result.priority_lost = true;
        result.trades = std::move(match_res.trades);

        notify_modified(id, side, old_price, new_price, old_qty, new_qty, true, ts);
        return result;
    }

    [[nodiscard]] std::optional<core::Price> get_best_bid() const noexcept override {
        if (!best_bid_level_) return std::nullopt;
        return best_bid_level_->price;
    }

    [[nodiscard]] std::optional<core::Price> get_best_ask() const noexcept override {
        if (!best_ask_level_) return std::nullopt;
        return best_ask_level_->price;
    }

    [[nodiscard]] std::optional<core::Price> get_spread() const noexcept override {
        if (best_bid_level_ && best_ask_level_) {
            return best_ask_level_->price - best_bid_level_->price;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_midpoint() const noexcept override {
        if (best_bid_level_ && best_ask_level_) {
            return (static_cast<double>(best_bid_level_->price) + static_cast<double>(best_ask_level_->price)) / 2.0;
        }
        return std::nullopt;
    }

    [[nodiscard]] market_data::MarketDepth get_depth(size_t max_levels = 10) const override {
        market_data::MarketDepth depth;
        depth.seq = seq_;

        size_t count = 0;
        PriceLevel* cur_bid = best_bid_level_;
        while (cur_bid && count++ < max_levels) {
            depth.bids.push_back({cur_bid->price, cur_bid->total_qty, cur_bid->order_count});
            cur_bid = cur_bid->next_level;
        }

        count = 0;
        PriceLevel* cur_ask = best_ask_level_;
        while (cur_ask && count++ < max_levels) {
            depth.asks.push_back({cur_ask->price, cur_ask->total_qty, cur_ask->order_count});
            cur_ask = cur_ask->next_level;
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
        const OrderNode* node = order_pool_.get(it->second);
        if (!node) return std::nullopt;
        return core::Order{
            .id = node->id,
            .price = node->price,
            .qty = node->qty,
            .initial_qty = node->initial_qty,
            .side = node->side,
            .tif = node->tif,
            .timestamp = node->timestamp,
            .seq = node->seq
        };
    }

    [[nodiscard]] core::SeqNum get_seq() const noexcept override {
        return seq_;
    }

    [[nodiscard]] uint64_t compute_state_hash() const override {
        uint64_t hash = FNV_OFFSET_BASIS;

        // Bids: descending price ladder
        PriceLevel* bid_lvl = best_bid_level_;
        while (bid_lvl) {
            fnv1a_combine(hash, static_cast<uint64_t>(bid_lvl->price));
            fnv1a_combine(hash, bid_lvl->total_qty);
            fnv1a_combine(hash, static_cast<uint64_t>(bid_lvl->order_count));
            for (const auto& ord : bid_lvl->queue) {
                fnv1a_combine(hash, ord.id);
                fnv1a_combine(hash, ord.qty);
            }
            bid_lvl = bid_lvl->next_level;
        }

        // Asks: ascending price ladder
        PriceLevel* ask_lvl = best_ask_level_;
        while (ask_lvl) {
            fnv1a_combine(hash, static_cast<uint64_t>(ask_lvl->price));
            fnv1a_combine(hash, ask_lvl->total_qty);
            fnv1a_combine(hash, static_cast<uint64_t>(ask_lvl->order_count));
            for (const auto& ord : ask_lvl->queue) {
                fnv1a_combine(hash, ord.id);
                fnv1a_combine(hash, ord.qty);
            }
            ask_lvl = ask_lvl->next_level;
        }

        return hash;
    }

    void clear() override {
        order_index_.clear();
        level_index_.clear();
        order_pool_.clear();
        level_pool_.clear();
        best_bid_level_ = nullptr;
        best_ask_level_ = nullptr;
        total_bid_volume_ = 0;
        total_ask_volume_ = 0;
    }

    void set_event_listener(core::EventListener* listener) override {
        listener_ = listener;
    }

    // Capacity & memory introspection
    [[nodiscard]] size_t pool_orders_allocated() const noexcept { return order_pool_.size(); }
    [[nodiscard]] size_t pool_levels_allocated() const noexcept { return level_pool_.size(); }

private:
    void match_buy(core::OrderId taker_id, core::Price limit_px, core::Qty& remaining_qty,
                   core::TimestampNs ts, core::MatchResult& result) {
        while (remaining_qty > 0 && best_ask_level_ && best_ask_level_->price <= limit_px) {
            PriceLevel* level = best_ask_level_;
            OrderNode* maker_order = level->queue.front();

            while (remaining_qty > 0 && maker_order) {
                core::Qty match_qty = std::min(remaining_qty, maker_order->qty);
                core::Price exec_px = maker_order->price;

                core::TradeEvent trade{
                    .seq = ++seq_,
                    .timestamp = ts,
                    .price = exec_px,
                    .qty = match_qty,
                    .maker_order_id = maker_order->id,
                    .taker_order_id = taker_id,
                    .maker_side = core::Side::Sell
                };
                result.trades.push_back(trade);
                if (listener_) listener_->on_trade(trade);

                maker_order->qty -= match_qty;
                level->total_qty -= match_qty;
                total_ask_volume_ -= match_qty;
                remaining_qty -= match_qty;

                if (maker_order->qty == 0) {
                    OrderNode* to_delete = maker_order;
                    maker_order = maker_order->next;
                    level->queue.unlink(to_delete);
                    level->order_count--;
                    order_index_.erase(to_delete->id);
                    order_pool_.deallocate(to_delete);
                } else {
                    break;
                }
            }

            if (level->order_count == 0) {
                remove_price_level(level);
            }
        }
    }

    void match_sell(core::OrderId taker_id, core::Price limit_px, core::Qty& remaining_qty,
                    core::TimestampNs ts, core::MatchResult& result) {
        while (remaining_qty > 0 && best_bid_level_ && best_bid_level_->price >= limit_px) {
            PriceLevel* level = best_bid_level_;
            OrderNode* maker_order = level->queue.front();

            while (remaining_qty > 0 && maker_order) {
                core::Qty match_qty = std::min(remaining_qty, maker_order->qty);
                core::Price exec_px = maker_order->price;

                core::TradeEvent trade{
                    .seq = ++seq_,
                    .timestamp = ts,
                    .price = exec_px,
                    .qty = match_qty,
                    .maker_order_id = maker_order->id,
                    .taker_order_id = taker_id,
                    .maker_side = core::Side::Buy
                };
                result.trades.push_back(trade);
                if (listener_) listener_->on_trade(trade);

                maker_order->qty -= match_qty;
                level->total_qty -= match_qty;
                total_bid_volume_ -= match_qty;
                remaining_qty -= match_qty;

                if (maker_order->qty == 0) {
                    OrderNode* to_delete = maker_order;
                    maker_order = maker_order->next;
                    level->queue.unlink(to_delete);
                    level->order_count--;
                    order_index_.erase(to_delete->id);
                    order_pool_.deallocate(to_delete);
                } else {
                    break;
                }
            }

            if (level->order_count == 0) {
                remove_price_level(level);
            }
        }
    }

    void rest_order(core::OrderId id, core::Side side, core::Price price,
                    core::Qty resting_qty, core::Qty initial_qty, core::TimestampNs ts) {
        PriceLevel* level = get_or_create_price_level(side, price);

        auto [node, handle] = order_pool_.allocate();
        if (!node) return; // Pool full (validated prior)

        node->id = id;
        node->price = price;
        node->qty = resting_qty;
        node->initial_qty = initial_qty;
        node->side = side;
        node->tif = core::TimeInForce::GTC;
        node->timestamp = ts;
        node->seq = ++seq_;
        node->level = level;
        node->handle = handle;

        level->queue.push_back(node);
        level->total_qty += resting_qty;
        level->order_count++;

        if (side == core::Side::Buy) {
            total_bid_volume_ += resting_qty;
        } else {
            total_ask_volume_ += resting_qty;
        }

        order_index_[id] = handle;
    }

    [[nodiscard]] PriceLevel* get_or_create_price_level(core::Side side, core::Price price) {
        uint64_t key = (static_cast<uint64_t>(side) << 63) | (static_cast<uint64_t>(price) & 0x7FFFFFFFFFFFFFFFULL);
        auto it = level_index_.find(key);
        if (it != level_index_.end()) {
            return it->second;
        }

        auto [level, handle] = level_pool_.allocate();
        level->price = price;
        level->side = side;
        level->total_qty = 0;
        level->order_count = 0;
        level->prev_level = nullptr;
        level->next_level = nullptr;
        level->handle = handle;
        level->queue.clear();

        insert_price_level_into_ladder(level);
        level_index_[key] = level;
        return level;
    }

    void insert_price_level_into_ladder(PriceLevel* level) {
        if (level->side == core::Side::Buy) {
            // Descending order: highest price first
            if (!best_bid_level_ || level->price > best_bid_level_->price) {
                level->next_level = best_bid_level_;
                level->prev_level = nullptr;
                if (best_bid_level_) best_bid_level_->prev_level = level;
                best_bid_level_ = level;
                return;
            }

            PriceLevel* cur = best_bid_level_;
            while (cur->next_level && cur->next_level->price > level->price) {
                cur = cur->next_level;
            }
            level->next_level = cur->next_level;
            level->prev_level = cur;
            if (cur->next_level) cur->next_level->prev_level = level;
            cur->next_level = level;
        } else {
            // Ascending order: lowest price first
            if (!best_ask_level_ || level->price < best_ask_level_->price) {
                level->next_level = best_ask_level_;
                level->prev_level = nullptr;
                if (best_ask_level_) best_ask_level_->prev_level = level;
                best_ask_level_ = level;
                return;
            }

            PriceLevel* cur = best_ask_level_;
            while (cur->next_level && cur->next_level->price < level->price) {
                cur = cur->next_level;
            }
            level->next_level = cur->next_level;
            level->prev_level = cur;
            if (cur->next_level) cur->next_level->prev_level = level;
            cur->next_level = level;
        }
    }

    void remove_price_level(PriceLevel* level) {
        uint64_t key = (static_cast<uint64_t>(level->side) << 63) | (static_cast<uint64_t>(level->price) & 0x7FFFFFFFFFFFFFFFULL);
        level_index_.erase(key);

        if (level->side == core::Side::Buy) {
            if (level == best_bid_level_) {
                best_bid_level_ = level->next_level;
                if (best_bid_level_) best_bid_level_->prev_level = nullptr;
            } else {
                if (level->prev_level) level->prev_level->next_level = level->next_level;
                if (level->next_level) level->next_level->prev_level = level->prev_level;
            }
        } else {
            if (level == best_ask_level_) {
                best_ask_level_ = level->next_level;
                if (best_ask_level_) best_ask_level_->prev_level = nullptr;
            } else {
                if (level->prev_level) level->prev_level->next_level = level->next_level;
                if (level->next_level) level->next_level->prev_level = level->prev_level;
            }
        }

        level_pool_.deallocate(level);
    }

    void notify_accepted(core::OrderId id, core::Side side, core::Price price, core::Qty qty, 
                         core::TimeInForce tif, core::TimestampNs ts) {
        if (listener_) listener_->on_order_accepted({++seq_, ts, id, side, price, qty, tif});
    }

    void notify_resting(core::OrderId id, core::Side side, core::Price price, core::Qty qty, core::TimestampNs ts) {
        if (listener_) listener_->on_order_resting({++seq_, ts, id, side, price, qty});
    }

    void notify_cancelled(core::OrderId id, core::Side side, core::Price price, core::Qty qty, 
                          core::RejectReason reason, core::TimestampNs ts) {
        if (listener_) listener_->on_order_cancelled({++seq_, ts, id, side, price, qty, reason});
    }

    void notify_modified(core::OrderId id, core::Side side, core::Price old_price, core::Price new_price,
                         core::Qty old_qty, core::Qty new_qty, bool priority_lost, core::TimestampNs ts) {
        if (listener_) listener_->on_order_modified({++seq_, ts, id, side, old_price, new_price, old_qty, new_qty, priority_lost});
    }

    void notify_rejected(core::OrderId id, core::RejectReason reason, core::TimestampNs ts) {
        if (listener_) listener_->on_order_rejected({++seq_, ts, id, reason});
    }

    core::EngineConfig config_;
    core::SeqNum seq_{0};
    core::EventListener* listener_{nullptr};

    memory::FixedPool<OrderNode> order_pool_;
    memory::FixedPool<PriceLevel> level_pool_;

    PriceLevel* best_bid_level_{nullptr};
    PriceLevel* best_ask_level_{nullptr};

    std::unordered_map<core::OrderId, memory::GenerationalHandle> order_index_;
    std::unordered_map<uint64_t, PriceLevel*> level_index_;

    core::Qty total_bid_volume_{0};
    core::Qty total_ask_volume_{0};
};

} // namespace atlas::book
