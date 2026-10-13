#pragma once

#include "replay/event_record.hpp"
#include <vector>
#include <random>
#include <fstream>
#include <cmath>

namespace atlas::replay {

struct WorkloadParams {
    size_t event_count{10'000};
    uint64_t seed{1337};
    core::Price mid_price{10'000};
    double price_stddev{15.0};
    core::Qty min_qty{1};
    core::Qty max_qty{100};
    double add_ratio{0.60};
    double cancel_ratio{0.25};
    double modify_ratio{0.15};
    double fak_ratio{0.10};
    core::TimestampNs initial_timestamp_ns{1'700'000'000'000'000'000ULL};
    core::TimestampNs avg_time_delta_ns{1'000}; // 1 microsecond avg arrival
};

class SyntheticGenerator {
public:
    static std::vector<EventRecord> generate(const WorkloadParams& params) {
        std::vector<EventRecord> events;
        events.reserve(params.event_count);

        std::mt19937_64 rng(params.seed);
        std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
        std::normal_distribution<double> price_dist(static_cast<double>(params.mid_price), params.price_stddev);
        std::uniform_int_distribution<uint64_t> qty_dist(params.min_qty, params.max_qty);
        std::exponential_distribution<double> time_delta_dist(1.0 / static_cast<double>(params.avg_time_delta_ns));

        std::vector<core::OrderId> active_order_ids;
        core::OrderId next_id = 1;
        core::SeqNum seq = 1;
        core::TimestampNs cur_time = params.initial_timestamp_ns;

        for (size_t i = 0; i < params.event_count; ++i) {
            cur_time += std::max<core::TimestampNs>(1, static_cast<core::TimestampNs>(time_delta_dist(rng)));
            double op_roll = uniform_dist(rng);

            if (op_roll < params.add_ratio || active_order_ids.empty()) {
                // ADD
                core::OrderId id = next_id++;
                core::Side side = (uniform_dist(rng) < 0.5) ? core::Side::Buy : core::Side::Sell;
                core::Price px = std::max<core::Price>(1, static_cast<core::Price>(std::round(price_dist(rng))));
                core::Qty qty = qty_dist(rng);
                core::TimeInForce tif = (uniform_dist(rng) < params.fak_ratio) ? core::TimeInForce::FAK : core::TimeInForce::GTC;

                EventRecord rec{
                    .seq = seq++,
                    .timestamp = cur_time,
                    .action = core::Action::Add,
                    .order_id = id,
                    .side = side,
                    .price = px,
                    .qty = qty,
                    .tif = tif
                };
                events.push_back(rec);
                if (tif == core::TimeInForce::GTC) {
                    active_order_ids.push_back(id);
                }
            } else if (op_roll < (params.add_ratio + params.cancel_ratio)) {
                // CANCEL
                std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids.size() - 1);
                size_t sel_idx = idx_dist(rng);
                core::OrderId target_id = active_order_ids[sel_idx];

                EventRecord rec{
                    .seq = seq++,
                    .timestamp = cur_time,
                    .action = core::Action::Cancel,
                    .order_id = target_id,
                    .side = core::Side::Buy,
                    .price = 0,
                    .qty = 0,
                    .tif = core::TimeInForce::GTC
                };
                events.push_back(rec);

                active_order_ids[sel_idx] = active_order_ids.back();
                active_order_ids.pop_back();
            } else {
                // MODIFY
                std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids.size() - 1);
                size_t sel_idx = idx_dist(rng);
                core::OrderId target_id = active_order_ids[sel_idx];

                core::Price new_px = std::max<core::Price>(1, static_cast<core::Price>(std::round(price_dist(rng))));
                core::Qty new_qty = qty_dist(rng);

                EventRecord rec{
                    .seq = seq++,
                    .timestamp = cur_time,
                    .action = core::Action::Modify,
                    .order_id = target_id,
                    .side = core::Side::Buy,
                    .price = new_px,
                    .qty = new_qty,
                    .tif = core::TimeInForce::GTC
                };
                events.push_back(rec);
            }
        }

        return events;
    }

    static bool write_to_file(const std::string& path, const std::vector<EventRecord>& events) {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "#seq,timestamp,action,order_id,side,price,qty,tif\n";
        for (const auto& ev : events) {
            out << ev.to_csv() << "\n";
        }
        return true;
    }

    static std::vector<EventRecord> read_from_file(const std::string& path) {
        std::vector<EventRecord> events;
        std::ifstream in(path);
        if (!in.is_open()) return events;

        std::string line;
        while (std::getline(in, line)) {
            auto rec = EventRecord::from_csv(line);
            if (rec) {
                events.push_back(*rec);
            }
        }
        return events;
    }
};

} // namespace atlas::replay
