#pragma once

#include "backtester.hpp"

#include <cmath>
#include <vector>

namespace hft {

struct QuotePair {
    double bid               = 0.0;
    double ask               = 0.0;
    double reservation_price = 0.0;
    double half_spread       = 0.0;
    double volatility        = 0.0;
};

// Rolling variance via circular-buffer running sums (sum and sum-of-squares).
// Evicts the oldest sample on each update. O(1) update and query, zero allocs after construction.
class RollingVolatility {
public:
    explicit RollingVolatility(std::size_t window)
        : window_(std::max<std::size_t>(2, window)), buf_(window_, 0.0) {}

    void update(double mid) {
        if (last_mid_ > 0.0 && mid > 0.0) {
            const double r   = std::log(mid / last_mid_);
            const double old = buf_[head_];   // value being evicted
            buf_[head_]      = r;
            head_            = (head_ + 1 == window_) ? 0 : head_ + 1;
            sum_    += r - old;
            sum_sq_ += r * r - old * old;
            if (count_ < window_) ++count_;
        }
        last_mid_ = mid;
    }

    double sigma() const {
        if (count_ < 2) return 0.0;
        const double n        = static_cast<double>(count_);
        const double mean     = sum_ / n;
        const double variance = std::max(0.0, sum_sq_ / n - mean * mean);
        return std::sqrt(variance);
    }

private:
    std::size_t         window_;
    std::vector<double> buf_;     // circular, pre-allocated to window_ entries
    std::size_t         head_  = 0;
    std::size_t         count_ = 0;
    double              sum_   = 0.0;
    double              sum_sq_= 0.0;
    double              last_mid_ = 0.0;
};

class AvellanedaStoikovStrategy {
public:
    explicit AvellanedaStoikovStrategy(const EngineConfig& cfg)
        : cfg_(cfg), volatility_(static_cast<std::size_t>(cfg.volatility_window)) {}

    void observe(const BookSnapshot& book) { volatility_.update(book.mid()); }

    QuotePair quote(const BookSnapshot& book, double inventory) const {
        const double sigma  = volatility_.sigma();
        const double sigma2 = sigma * sigma;

        const double reference = cfg_.use_microprice_extension
            ? (1.0 - cfg_.microprice_weight) * book.mid()
              + cfg_.microprice_weight * book.microprice()
            : book.mid();

        // All intermediate terms are computed in log-return (relative, fractional) space.
        // sigma is the per-event log-return std-dev; gamma and kappa are dimensionless.
        // horizon_seconds is treated as a number of forward events (T in the AS model).
        const double horizon_events = std::max(1.0, cfg_.horizon_seconds);

        // Reservation price: skew reference away from inventory using the risk penalty.
        // Multiply the relative adjustment by reference to produce a price-unit result.
        const double reservation = reference * (1.0 - inventory * cfg_.gamma * sigma2 * horizon_events);

        // Spread components in relative space, then scaled to absolute price units.
        const double risk_spread_rel = cfg_.gamma * sigma2 * horizon_events;
        const double liq_spread_rel  =
            (2.0 / cfg_.gamma) * std::log(1.0 + cfg_.gamma / std::max(1e-9, cfg_.kappa));
        const double half_spread = std::max(book.spread() * 0.5,
                                            0.5 * (risk_spread_rel + liq_spread_rel) * reference);

        QuotePair q;
        q.reservation_price = reservation;
        q.half_spread       = half_spread;
        q.volatility        = sigma;
        q.bid = std::min(book.best_bid(), round_to_tick(reservation - half_spread, cfg_.tick_size));
        q.ask = std::max(book.best_ask(), round_to_tick(reservation + half_spread, cfg_.tick_size));
        return q;
    }

private:
    EngineConfig      cfg_;
    RollingVolatility volatility_;
};

}  // namespace hft
