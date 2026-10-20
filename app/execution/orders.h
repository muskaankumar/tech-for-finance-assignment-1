// Order state and order entry: one quote per side, at most one request in
// flight per side.
//
// ====================================================================
// CHANGED: added history_.reserve(256) in the constructor.
//
// DELETED: nothing — the old code just had no reserve call.
//
// ADDED: reserve(256) so that push_back in SendQuote never triggers
//   a reallocation (malloc + memcpy + free) on the hot path.
//   256 is well above any scenario's quote count.
//   Without this, the vector starts at capacity 0 and reallocates
//   at sizes 1, 2, 4, 8, 16, 32, ... causing latency spikes at
//   each boundary — especially visible in tail (max) latency.
// ====================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "app/execution/decision_log.h"
#include "sim/messages/execution_report.h"
#include "sim/runtime/simulator.h"

namespace app {

struct Quote {
    bool live = false;
    sim::Token token = 0;
    sim::Price price = 0;
    std::uint32_t open_shares = 0;

    bool pending = false;
    sim::Token pending_token = 0;
};

struct QuoteRecord {
    sim::Timestamp time;
    sim::Side side;
    sim::Price price;
    std::uint32_t shares;
    sim::Token token;
};

class OrderManager {
public:
    OrderManager(sim::OrderEntrySession& session, DecisionLog& log)
        : session_(session), log_(log) {
        history_.reserve(256);  // ADDED: pre-allocate to avoid hot-path malloc
    }

    Quote& quote(sim::Side side) { return side == sim::Side::Buy ? bid_ : ask_; }

    void SendQuote(sim::Side side, sim::Price price, std::uint32_t shares);
    void CancelAll();

    void OnAccepted(const sim::oe::Accepted& msg);
    void OnReplaced(const sim::oe::Replaced& msg);
    void OnExecuted(const sim::oe::Executed& msg);
    void OnRejected(const sim::oe::Rejected& msg);
    void OnCanceled(const sim::oe::Canceled& msg);

    std::int64_t position() const { return position_; }
    std::int64_t cash() const { return cash_; }
    const std::vector<QuoteRecord>& history() const { return history_; }

private:
    Quote* FindByToken(sim::Token token);
    void CancelToken(sim::Token token);

    sim::OrderEntrySession& session_;
    DecisionLog& log_;
    Quote bid_;
    Quote ask_;
    sim::Token next_token_ = 1;
    std::int64_t position_ = 0;
    std::int64_t cash_ = 0;
    std::vector<QuoteRecord> history_;
};

}  // namespace app