#pragma once

#include "core/types.hpp"
#include <vector>
#include <string>
#include <sstream>

namespace atlas::core {

struct TradeEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    Price price{0};
    Qty qty{0};
    OrderId maker_order_id{0};
    OrderId taker_order_id{0};
    Side maker_side{Side::Sell};

    [[nodiscard]] std::string to_string() const {
        std::ostringstream ss;
        ss << "Trade[seq=" << seq << ", px=" << price << ", qty=" << qty 
           << ", maker=" << maker_order_id << " (" << (maker_side == Side::Buy ? "BUY" : "SELL")
           << "), taker=" << taker_order_id << "]";
        return ss.str();
    }
};

struct OrderAcceptedEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty qty{0};
    TimeInForce tif{TimeInForce::GTC};
};

struct OrderRestingEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty resting_qty{0};
};

struct OrderFilledEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty filled_qty{0};
    Qty remaining_qty{0};
};

struct OrderCancelledEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty cancelled_qty{0};
    RejectReason reason{RejectReason::None};
};

struct OrderModifiedEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price old_price{0};
    Price new_price{0};
    Qty old_qty{0};
    Qty new_qty{0};
    bool priority_lost{false};
};

struct OrderRejectedEvent {
    SeqNum seq{0};
    TimestampNs timestamp{0};
    OrderId order_id{0};
    RejectReason reason{RejectReason::None};
};

struct MatchResult {
    bool success{false};
    RejectReason reject_reason{RejectReason::None};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty requested_qty{0};
    Qty executed_qty{0};
    Qty resting_qty{0};
    Qty cancelled_qty{0};
    std::vector<TradeEvent> trades{};
};

struct CancelResult {
    bool success{false};
    RejectReason reject_reason{RejectReason::None};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Qty cancelled_qty{0};
};

struct ModifyResult {
    bool success{false};
    RejectReason reject_reason{RejectReason::None};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price old_price{0};
    Price new_price{0};
    Qty old_qty{0};
    Qty new_qty{0};
    bool priority_lost{false};
    std::vector<TradeEvent> trades{}; // If aggressive modification crossed book
};

// Event listener interface for market data & replay recording
class EventListener {
public:
    virtual ~EventListener() = default;
    virtual void on_trade(const TradeEvent&) {}
    virtual void on_order_accepted(const OrderAcceptedEvent&) {}
    virtual void on_order_resting(const OrderRestingEvent&) {}
    virtual void on_order_filled(const OrderFilledEvent&) {}
    virtual void on_order_cancelled(const OrderCancelledEvent&) {}
    virtual void on_order_modified(const OrderModifiedEvent&) {}
    virtual void on_order_rejected(const OrderRejectedEvent&) {}
};

} // namespace atlas::core
