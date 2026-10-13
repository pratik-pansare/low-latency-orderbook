#pragma once

#include "core/types.hpp"

namespace atlas::core {

struct Order {
    OrderId id{INVALID_ORDER_ID};
    Price price{0};
    Qty qty{0};
    Qty initial_qty{0};
    Side side{Side::Buy};
    TimeInForce tif{TimeInForce::GTC};
    TimestampNs timestamp{0};
    SeqNum seq{0};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return id != INVALID_ORDER_ID && price > 0 && qty > 0;
    }
};

} // namespace atlas::core
