#pragma once

#include "backtester.hpp"

#include <cmath>
#include <deque>
#include <utility>

namespace hft {

struct QuotePair {
    double bid = 0.0;
    double ask = 0.0;
    double reservation_price = 0.0;
    double half_spread = 0.0;
    double volatility = 0.0;
};

class RollingVolatility {
public:
    explicit RollingVolatility(std::size_t window) : window_(std::max<std::size_t>(2, window)) {}

    void update(double mid) {
        if (last_mid_ > 0.0 && mid > 0.0) {
            const double r = std::log(mid / last_mid_);
            values_.push_back(r);
            sum_ += r;
            sum_sq_ += r * r;
            while (values_.size() > window_) {
                const double old = values_.front();
                values_.pop_front();
                sum_ -= old;
                sum_sq_ -= old * old;
            }
        }
        last_mid_ = mid;
    }

    double sigma() const {
        if (values_.size() < 2) {
            return 0.0;
        }
        const double n = static_cast<double>(values_.size());
        const double mean = sum_ / n;
        const double variance = std::max(0.0, sum_sq_ / n - mean * mean);
        return std::sqrt(variance);
    }

private:
    std::size_t window_ = 0;
    std::deque<double> values_;
    double sum_ = 0.0;
    double sum_sq_ = 0.0;
    double last_mid_ = 0.0;
};

class AvellanedaStoikovStrategy {
public:
    explicit AvellanedaStoikovStrategy(const EngineConfig& cfg)
        : cfg_(cfg), volatility_(static_cast<std::size_t>(cfg.volatility_window)) {}

    void observe(const BookSnapshot& book) {
        volatility_.update(book.mid());
    }

    QuotePair quote(const BookSnapshot& book, double inventory) const {
        const double sigma = volatility_.sigma();
        const double sigma2 = sigma * sigma;
        const double reference = cfg_.use_microprice_extension
            ? (1.0 - cfg_.microprice_weight) * book.mid() + cfg_.microprice_weight * book.microprice()
            : book.mid();

        const double horizon_events = std::max(1.0, cfg_.horizon_seconds);
        const double reservation = reference - inventory * cfg_.gamma * sigma2 * horizon_events;
        const double risk_spread = cfg_.gamma * sigma2 * horizon_events;
        const double liquidity_spread = (2.0 / cfg_.gamma) * std::log(1.0 + cfg_.gamma / std::max(1e-9, cfg_.kappa));
        const double half_spread = std::max(book.spread() * 0.5, 0.5 * (risk_spread + liquidity_spread * cfg_.tick_size));

        QuotePair q;
        q.reservation_price = reservation;
        q.half_spread = half_spread;
        q.volatility = sigma;
        q.bid = std::min(book.best_bid(), round_to_tick(reservation - half_spread, cfg_.tick_size));
        q.ask = std::max(book.best_ask(), round_to_tick(reservation + half_spread, cfg_.tick_size));
        return q;
    }

private:
    EngineConfig cfg_;
    RollingVolatility volatility_;
};

}  // namespace hft
