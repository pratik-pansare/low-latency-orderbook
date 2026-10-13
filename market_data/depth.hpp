#pragma once

#include "core/types.hpp"
#include <vector>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>

namespace atlas::market_data {

struct LevelData {
    core::Price price{0};
    core::Qty total_qty{0};
    size_t order_count{0};

    bool operator==(const LevelData& other) const = default;
};

struct MarketDepth {
    core::TimestampNs timestamp{0};
    core::SeqNum seq{0};
    std::vector<LevelData> bids{};
    std::vector<LevelData> asks{};

    [[nodiscard]] std::optional<core::Price> best_bid() const noexcept {
        if (bids.empty()) return std::nullopt;
        return bids.front().price;
    }

    [[nodiscard]] std::optional<core::Price> best_ask() const noexcept {
        if (asks.empty()) return std::nullopt;
        return asks.front().price;
    }

    [[nodiscard]] std::optional<core::Price> spread() const noexcept {
        if (bids.empty() || asks.empty()) return std::nullopt;
        return asks.front().price - bids.front().price;
    }

    [[nodiscard]] std::optional<double> midpoint() const noexcept {
        if (bids.empty() || asks.empty()) return std::nullopt;
        return (static_cast<double>(bids.front().price) + static_cast<double>(asks.front().price)) / 2.0;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream ss;
        ss << "=== Market Depth (seq=" << seq << ") ===\n";
        ss << std::setw(10) << "BID_QTY" << " | "
           << std::setw(8) << "BID_PX" << " || "
           << std::setw(8) << "ASK_PX" << " | "
           << std::setw(10) << "ASK_QTY" << "\n";
        ss << "----------------------------------------------\n";
        size_t max_rows = std::max(bids.size(), asks.size());
        for (size_t i = 0; i < max_rows; ++i) {
            if (i < bids.size()) {
                ss << std::setw(10) << bids[i].total_qty << " | "
                   << std::setw(8) << bids[i].price << " || ";
            } else {
                ss << std::setw(10) << "-" << " | "
                   << std::setw(8) << "-" << " || ";
            }
            if (i < asks.size()) {
                ss << std::setw(8) << asks[i].price << " | "
                   << std::setw(10) << asks[i].total_qty << "\n";
            } else {
                ss << std::setw(8) << "-" << " | "
                   << std::setw(10) << "-" << "\n";
            }
        }
        return ss.str();
    }
};

} // namespace atlas::market_data
