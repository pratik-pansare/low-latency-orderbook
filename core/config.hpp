#pragma once

#include <cstddef>
#include <cstdint>

namespace atlas::core {

enum class SelfTradePolicy : uint8_t {
    Allow = 0,
    RejectNew = 1,
    CancelOldest = 2
};

struct EngineConfig {
    // Priority policy for order modification:
    // If true, reducing quantity retains priority; increasing quantity or changing price always loses priority.
    bool qty_reduction_retains_priority{true};

    // Self-trade prevention policy
    SelfTradePolicy self_trade_policy{SelfTradePolicy::Allow};

    // Capacity sizing for preallocated memory pools
    size_t max_orders{1'000'000};
    size_t max_price_levels{65'536};
};

} // namespace atlas::core
