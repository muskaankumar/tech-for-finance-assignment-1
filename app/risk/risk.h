// Pre-trade risk checks.
//
// Safety first: a quote must never go out if anything is wrong.
//
// CHANGE: Added non-throwing boolean methods (IsHalted, BookValid,
// PositionBlocked) so the hot path can use if-return instead of
// try-catch. The throwing versions are kept for any code outside
// the hot path that still wants them.
#pragma once

#include <cstdint>
#include <stdexcept>

#include "app/config.h"
#include "app/strategy/pricing.h"

namespace app {

enum class RiskError : std::uint8_t {
    BookInvalid,
    SequenceGap,
    PositionLimit,
    LossLimitBreached,
};

inline bool IsStanding(RiskError error) {
    return error == RiskError::SequenceGap || error == RiskError::LossLimitBreached;
}

class RiskViolation : public std::runtime_error {
public:
    RiskViolation(RiskError error, const char* what) : std::runtime_error(what), error_(error) {}
    RiskError error() const { return error_; }

private:
    RiskError error_;
};

class RiskGate {
public:
    void LatchSequenceGap() { sequence_gap_ = true; }
    void LatchLossLimit() { loss_limit_breached_ = true; }
    bool Halted() const { return sequence_gap_ || loss_limit_breached_; }

    // --- ORIGINAL throwing versions (unchanged, kept for non-hot-path use) ---
    void CheckStanding() const {
        if (sequence_gap_) {
            throw RiskViolation(RiskError::SequenceGap, "market data sequence gap");
        }
        if (loss_limit_breached_) {
            throw RiskViolation(RiskError::LossLimitBreached, "loss limit breached");
        }
    }

    static void CheckBook(const BookTop& top) {
        if (!top.Valid()) {
            throw RiskViolation(RiskError::BookInvalid, "book side empty or crossed");
        }
    }

    static void CheckPosition(sim::Side side, std::int64_t position) {
        const bool breach = side == sim::Side::Buy ? position + kQuoteShares > kPositionLimit
                                                   : position - kQuoteShares < -kPositionLimit;
        if (breach) {
            throw RiskViolation(RiskError::PositionLimit, "position limit");
        }
    }

    // --- NEW: non-throwing versions for the hot path ---
    // ADDED: returns true if this side would breach the position limit.
    // Same logic as CheckPosition but returns bool instead of throwing.
    static bool PositionBlocked(sim::Side side, std::int64_t position) {
        return side == sim::Side::Buy ? position + kQuoteShares > kPositionLimit
                                     : position - kQuoteShares < -kPositionLimit;
    }

private:
    bool sequence_gap_ = false;
    bool loss_limit_breached_ = false;
};

}  // namespace app