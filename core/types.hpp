#pragma once

#include <cstdint>
#include <string_view>
#include <string>
#include <limits>
#include <ostream>

namespace atlas::core {

using OrderId = uint64_t;
using Price = int64_t;      // Price in integer ticks (e.g., cents, fractions of cents)
using Qty = uint64_t;        // Discrete quantity / volume
using SeqNum = uint64_t;     // Monotonic sequence number
using TimestampNs = uint64_t;// Nanoseconds since epoch or sim clock

constexpr OrderId INVALID_ORDER_ID = 0;
constexpr Price INVALID_PRICE = std::numeric_limits<Price>::min();
constexpr Price MIN_PRICE = 1;
constexpr Price MAX_PRICE = std::numeric_limits<Price>::max() - 1;

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

[[nodiscard]] constexpr Side opposite_side(Side side) noexcept {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view to_string(Side side) noexcept {
    return side == Side::Buy ? "BUY" : "SELL";
}

inline std::ostream& operator<<(std::ostream& os, Side side) {
    return os << to_string(side);
}

enum class TimeInForce : uint8_t {
    GTC = 0, // Good 'Til Cancelled: immediately matches, remainder rests
    FAK = 1  // Fill-And-Kill: immediately matches, remainder cancelled
};

[[nodiscard]] constexpr std::string_view to_string(TimeInForce tif) noexcept {
    return tif == TimeInForce::GTC ? "GTC" : "FAK";
}

inline std::ostream& operator<<(std::ostream& os, TimeInForce tif) {
    return os << to_string(tif);
}

enum class Action : uint8_t {
    Add = 0,
    Cancel = 1,
    Modify = 2
};

[[nodiscard]] constexpr std::string_view to_string(Action action) noexcept {
    switch (action) {
        case Action::Add: return "ADD";
        case Action::Cancel: return "CANCEL";
        case Action::Modify: return "MODIFY";
    }
    return "UNKNOWN";
}

enum class RejectReason : uint8_t {
    None = 0,
    InvalidQuantity,
    InvalidPrice,
    DuplicateOrderId,
    OrderNotFound,
    SelfTrade,
    InvalidModification,
    BookFull,
    VenueHalted
};

[[nodiscard]] constexpr std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
        case RejectReason::None: return "None";
        case RejectReason::InvalidQuantity: return "InvalidQuantity";
        case RejectReason::InvalidPrice: return "InvalidPrice";
        case RejectReason::DuplicateOrderId: return "DuplicateOrderId";
        case RejectReason::OrderNotFound: return "OrderNotFound";
        case RejectReason::SelfTrade: return "SelfTrade";
        case RejectReason::InvalidModification: return "InvalidModification";
        case RejectReason::BookFull: return "BookFull";
        case RejectReason::VenueHalted: return "VenueHalted";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, RejectReason reason) {
    return os << to_string(reason);
}

} // namespace atlas::core
