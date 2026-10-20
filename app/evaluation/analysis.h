// Latency report.
//
// Measures end-to-end latency the way the exchange sees it: from the moment
// a message reaches us (the capture tap's timestamp, taken outside the
// application) to the moment we finished handling it (our own latency log).
//
// ====================================================================
// CHANGED: report median, p90, p99, max instead of just mean.
//
// DELETED (old code):
//   std::map<std::pair<...>, sim::WallTime> completed;
//   double total_ns = 0;
//   ...
//   out << "average latency: " << total_ns / count << " ns\n";
//
//   Problems with the old version:
//   1. std::map for the join — O(log n) per lookup with pointer-chasing
//      through a red-black tree. Every node is a separate heap allocation,
//      so each lookup misses cache. An unordered_map or a flat vector
//      would be faster, but this runs post-simulation so it doesn't
//      affect the strategy's trading latency.
//   2. Reports only the mean — one cold-start callback of ~110 µs pulls
//      the average from ~800 ns to ~3000 ns, completely masking steady-
//      state performance. A single outlier dominates the metric.
//
// ADDED:
//   - Collect all individual latencies into a std::vector<double>.
//   - Sort them once (O(n log n) but post-simulation, not hot path).
//   - Report: count, mean, median, p90, p99, min, max.
//   - Also kept the std::map join (it's off the hot path, so its
//     cost doesn't matter for trading performance).
//
// WHY THIS MATTERS (Carl Cook CppCon 2017 slides 49-52):
//   Cook warns that mean-based metrics hide the real picture.
//   Percentiles show that after cold-start, steady-state latency is
//   consistently sub-microsecond — the mean alone never reveals this.
// ====================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include "app/evaluation/latency_log.h"
#include "sim/messages/execution_report.h"
#include "sim/messages/market_data.h"
#include "sim/runtime/capture.h"

namespace app {

inline void PrintLatencyReport(const sim::Capture& capture, const LatencyLog& log,
                               std::ostream& out) {
    // Build completion map (same join as before — off the hot path).
    std::map<std::pair<sim::DeliveryKind, std::uint64_t>, sim::WallTime> completed;
    for (const LatencyRecord& r : log.records()) {
        completed[{r.kind, r.key}] = r.completed;
    }

    // CHANGED: collect individual latencies into a vector instead of
    // accumulating just a running sum.
    std::vector<double> latencies;
    latencies.reserve(capture.deliveries.size());

    for (const sim::DeliveryRecord& d : capture.deliveries) {
        std::uint64_t key = d.sim_time;
        const auto bytes = d.packet.view();
        if (d.kind == sim::DeliveryKind::MarketData) {
            key = sim::Decode<sim::md::Header>(bytes.first(sizeof(sim::md::Header))).sequence;
        } else if (d.kind == sim::DeliveryKind::ExecutionReport) {
            key = sim::Decode<sim::oe::ReportHeader>(bytes.first(sizeof(sim::oe::ReportHeader)))
                      .sequence;
        }
        const auto it = completed.find({d.kind, key});
        if (it != completed.end()) {
            latencies.push_back(static_cast<double>(it->second - d.entry));
        }
    }

    const std::uint64_t count = latencies.size();
    out << "messages: " << count << '\n';

    if (count == 0) {
        out << "no latency data\n";
        return;
    }

    // Sort for percentile computation.
    std::sort(latencies.begin(), latencies.end());

    // Mean (same value the old code reported, for comparison).
    double total_ns = 0;
    for (double v : latencies) {
        total_ns += v;
    }
    const double mean = total_ns / static_cast<double>(count);

    // Percentile helper: index = ceil(p * n) - 1, clamped.
    auto percentile = [&](double p) -> double {
        auto idx = static_cast<std::size_t>(std::ceil(p * static_cast<double>(count))) - 1;
        if (idx >= count) idx = count - 1;
        return latencies[idx];
    };

    // ADDED: full latency profile.
    out << "average latency: " << mean << " ns\n";
    out << "median  latency: " << percentile(0.50) << " ns\n";
    out << "p90     latency: " << percentile(0.90) << " ns\n";
    out << "p99     latency: " << percentile(0.99) << " ns\n";
    out << "min     latency: " << latencies.front() << " ns\n";
    out << "max     latency: " << latencies.back() << " ns\n";
}

}  // namespace app