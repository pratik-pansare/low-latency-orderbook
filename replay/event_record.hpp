#pragma once

#include "core/types.hpp"
#include <string>
#include <sstream>
#include <charconv>
#include <string_view>
#include <optional>

namespace atlas::replay {

struct EventRecord {
    core::SeqNum seq{0};
    core::TimestampNs timestamp{0};
    core::Action action{core::Action::Add};
    core::OrderId order_id{0};
    core::Side side{core::Side::Buy};
    core::Price price{0};
    core::Qty qty{0};
    core::TimeInForce tif{core::TimeInForce::GTC};

    [[nodiscard]] std::string to_csv() const {
        std::ostringstream ss;
        ss << seq << ","
           << timestamp << ","
           << core::to_string(action) << ","
           << order_id << ","
           << core::to_string(side) << ","
           << price << ","
           << qty << ","
           << core::to_string(tif);
        return ss.str();
    }

    [[nodiscard]] static std::optional<EventRecord> from_csv(std::string_view line) {
        if (line.empty() || line.starts_with("#") || line.starts_with("seq")) {
            return std::nullopt; // Skip empty, comment or header lines
        }

        EventRecord rec;
        std::string_view remaining = line;

        auto next_token = [&remaining]() -> std::string_view {
            auto pos = remaining.find(',');
            if (pos == std::string_view::npos) {
                std::string_view tok = remaining;
                remaining = {};
                return tok;
            }
            std::string_view tok = remaining.substr(0, pos);
            remaining.remove_prefix(pos + 1);
            return tok;
        };

        // 1. seq
        auto tok = next_token();
        std::from_chars(tok.data(), tok.data() + tok.size(), rec.seq);

        // 2. timestamp
        tok = next_token();
        std::from_chars(tok.data(), tok.data() + tok.size(), rec.timestamp);

        // 3. action
        tok = next_token();
        if (tok == "ADD") rec.action = core::Action::Add;
        else if (tok == "CANCEL") rec.action = core::Action::Cancel;
        else if (tok == "MODIFY") rec.action = core::Action::Modify;
        else return std::nullopt;

        // 4. order_id
        tok = next_token();
        std::from_chars(tok.data(), tok.data() + tok.size(), rec.order_id);

        // 5. side
        tok = next_token();
        if (tok == "BUY") rec.side = core::Side::Buy;
        else if (tok == "SELL") rec.side = core::Side::Sell;
        else return std::nullopt;

        // 6. price
        tok = next_token();
        std::from_chars(tok.data(), tok.data() + tok.size(), rec.price);

        // 7. qty
        tok = next_token();
        std::from_chars(tok.data(), tok.data() + tok.size(), rec.qty);

        // 8. tif
        tok = next_token();
        if (tok == "GTC") rec.tif = core::TimeInForce::GTC;
        else if (tok == "FAK") rec.tif = core::TimeInForce::FAK;
        else rec.tif = core::TimeInForce::GTC;

        return rec;
    }
};

} // namespace atlas::replay
