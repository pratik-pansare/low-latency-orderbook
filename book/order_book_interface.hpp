#pragma once

#include "core/types.hpp"
#include "core/events.hpp"
#include "core/order.hpp"
#include "market_data/depth.hpp"
#include <optional>
#include <cstdint>

namespace atlas::book {

class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    // Core Matching Engine API
    virtual core::MatchResult add_order(core::OrderId id, 
                                        core::Side side, 
                                        core::Price price, 
                                        core::Qty qty, 
                                        core::TimeInForce tif = core::TimeInForce::GTC, 
                                        core::TimestampNs ts = 0) = 0;

    virtual core::CancelResult cancel_order(core::OrderId id, 
                                            core::TimestampNs ts = 0) = 0;

    virtual core::ModifyResult modify_order(core::OrderId id, 
                                            core::Price new_price, 
                                            core::Qty new_qty, 
                                            core::TimestampNs ts = 0) = 0;

    // Queries
    [[nodiscard]] virtual std::optional<core::Price> get_best_bid() const noexcept = 0;
    [[nodiscard]] virtual std::optional<core::Price> get_best_ask() const noexcept = 0;
    [[nodiscard]] virtual std::optional<core::Price> get_spread() const noexcept = 0;
    [[nodiscard]] virtual std::optional<double> get_midpoint() const noexcept = 0;
    [[nodiscard]] virtual market_data::MarketDepth get_depth(size_t max_levels = 10) const = 0;

    [[nodiscard]] virtual size_t order_count() const noexcept = 0;
    [[nodiscard]] virtual core::Qty total_volume() const noexcept = 0;
    [[nodiscard]] virtual core::Qty bid_volume() const noexcept = 0;
    [[nodiscard]] virtual core::Qty ask_volume() const noexcept = 0;

    [[nodiscard]] virtual bool has_order(core::OrderId id) const noexcept = 0;
    [[nodiscard]] virtual std::optional<core::Order> get_order(core::OrderId id) const = 0;
    [[nodiscard]] virtual core::SeqNum get_seq() const noexcept = 0;

    // State verification & reset
    [[nodiscard]] virtual uint64_t compute_state_hash() const = 0;
    virtual void clear() = 0;

    // Event listener registration
    virtual void set_event_listener(core::EventListener* listener) = 0;
};

// 64-bit FNV-1a hash helper for deterministic state hashing
inline void fnv1a_combine(uint64_t& hash, uint64_t val) noexcept {
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        hash ^= byte;
        hash *= FNV_PRIME;
    }
}

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

} // namespace atlas::book
