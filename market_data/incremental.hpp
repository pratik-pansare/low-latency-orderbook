#pragma once

#include "core/types.hpp"
#include <string>
#include <sstream>

namespace atlas::market_data {

enum class UpdateAction : uint8_t {
    Add = 0,
    Modify = 1,
    Delete = 2
};

struct LevelUpdate {
    core::TimestampNs timestamp{0};
    core::SeqNum seq{0};
    core::Side side{core::Side::Buy};
    core::Price price{0};
    core::Qty total_qty{0};
    size_t order_count{0};
    UpdateAction action{UpdateAction::Add};

    [[nodiscard]] std::string to_string() const {
        std::ostringstream ss;
        ss << "L2Update[seq=" << seq << ", side=" << (side == core::Side::Buy ? "BUY" : "SELL")
           << ", px=" << price << ", qty=" << total_qty << ", cnt=" << order_count
           << ", act=" << (action == UpdateAction::Add ? "ADD" : action == UpdateAction::Modify ? "MOD" : "DEL")
           << "]";
        return ss.str();
    }
};

} // namespace atlas::market_data
