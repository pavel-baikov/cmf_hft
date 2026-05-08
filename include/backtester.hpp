#pragma once

#include "csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hft {

enum class Side { Buy, Sell };

struct Level {
    double price = 0.0;
    double quantity = 0.0;
};

struct BookSnapshot {
    std::int64_t timestamp = 0;
    std::vector<Level> asks;
    std::vector<Level> bids;

    double best_ask() const { return asks.empty() ? std::numeric_limits<double>::quiet_NaN() : asks.front().price; }
    double best_bid() const { return bids.empty() ? std::numeric_limits<double>::quiet_NaN() : bids.front().price; }
    double mid() const { return 0.5 * (best_bid() + best_ask()); }
    double spread() const { return best_ask() - best_bid(); }
    double microprice() const {
        if (asks.empty() || bids.empty()) {
            return mid();
        }
        const double bid_qty = std::max(0.0, bids.front().quantity);
        const double ask_qty = std::max(0.0, asks.front().quantity);
        const double denom = bid_qty + ask_qty;
        if (denom <= 0.0) {
            return mid();
        }
        return (best_ask() * bid_qty + best_bid() * ask_qty) / denom;
    }
};

struct Order {
    int id = 0;
    Side side = Side::Buy;
    double price = 0.0;
    double quantity = 0.0;
    std::int64_t placed_ts = 0;
};

struct Fill {
    int order_id = 0;
    Side side = Side::Buy;
    double price = 0.0;
    double quantity = 0.0;
    std::int64_t timestamp = 0;
};

struct Metrics {
    double cash = 0.0;
    double inventory = 0.0;
    double turnover = 0.0;
    double realized_fees = 0.0;
    double last_mid = 0.0;
    double max_abs_inventory = 0.0;
    std::size_t fills = 0;
    std::size_t snapshots = 0;
    std::size_t orders_placed = 0;
    std::size_t cancels = 0;

    double mark_to_market_pnl() const {
        return cash + inventory * last_mid;
    }
};

struct EngineConfig {
    std::string lob_path = "lob.csv";
    std::string report_path = "reports/performance.md";
    std::size_t levels = 25;
    std::size_t max_events = 200000;
    std::size_t warmup_events = 1000;
    std::size_t quote_interval_events = 25;
    double tick_size = 0.0000001;
    double order_quantity = 5000.0;
    double fee_bps = 0.0;
    double gamma = 0.08;
    double kappa = 1.5;
    double volatility_window = 500.0;
    double horizon_seconds = 60.0;
    double microprice_weight = 0.65;
    double inventory_limit = 50000.0;
    bool use_microprice_extension = true;
};

inline std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline bool parse_bool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

inline EngineConfig load_config(const std::string& path) {
    EngineConfig cfg;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open config: " + path);
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("invalid config line: " + line);
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "lob_path") cfg.lob_path = value;
        else if (key == "report_path") cfg.report_path = value;
        else if (key == "levels") cfg.levels = static_cast<std::size_t>(std::stoull(value));
        else if (key == "max_events") cfg.max_events = static_cast<std::size_t>(std::stoull(value));
        else if (key == "warmup_events") cfg.warmup_events = static_cast<std::size_t>(std::stoull(value));
        else if (key == "quote_interval_events") cfg.quote_interval_events = static_cast<std::size_t>(std::stoull(value));
        else if (key == "tick_size") cfg.tick_size = std::stod(value);
        else if (key == "order_quantity") cfg.order_quantity = std::stod(value);
        else if (key == "fee_bps") cfg.fee_bps = std::stod(value);
        else if (key == "gamma") cfg.gamma = std::stod(value);
        else if (key == "kappa") cfg.kappa = std::stod(value);
        else if (key == "volatility_window") cfg.volatility_window = std::stod(value);
        else if (key == "horizon_seconds") cfg.horizon_seconds = std::stod(value);
        else if (key == "microprice_weight") cfg.microprice_weight = std::stod(value);
        else if (key == "inventory_limit") cfg.inventory_limit = std::stod(value);
        else if (key == "use_microprice_extension") cfg.use_microprice_extension = parse_bool(value);
        else throw std::runtime_error("unknown config key: " + key);
    }
    return cfg;
}

class OrderManager {
public:
    int place(Side side, double price, double quantity, std::int64_t ts) {
        const int id = ++next_id_;
        open_.emplace(id, Order{id, side, price, quantity, ts});
        return id;
    }

    std::size_t cancel_all() {
        const auto count = open_.size();
        open_.clear();
        return count;
    }

    std::vector<Fill> match_crossing_orders(const BookSnapshot& book) {
        std::vector<int> done;
        std::vector<Fill> fills;
        for (const auto& [id, order] : open_) {
            const bool crosses = order.side == Side::Buy ? book.best_ask() <= order.price : book.best_bid() >= order.price;
            if (!crosses) {
                continue;
            }
            const double execution_price = order.side == Side::Buy ? std::min(order.price, book.best_ask()) : std::max(order.price, book.best_bid());
            fills.push_back(Fill{id, order.side, execution_price, order.quantity, book.timestamp});
            done.push_back(id);
        }
        for (const int id : done) {
            open_.erase(id);
        }
        return fills;
    }

    const std::map<int, Order>& open_orders() const { return open_; }

private:
    int next_id_ = 0;
    std::map<int, Order> open_;
};

class CsvBookReader {
public:
    explicit CsvBookReader(const std::string& path, std::size_t levels) : in_(path), levels_(levels) {
        if (!in_) {
            throw std::runtime_error("cannot open LOB file: " + path);
        }
        std::getline(in_, header_);
    }

    bool next(BookSnapshot& out) {
        std::string line;
        if (!std::getline(in_, line)) {
            return false;
        }
        const auto fields = split_csv_line(line);
        const std::size_t needed = 2 + levels_ * 4;
        if (fields.size() < needed) {
            throw std::runtime_error("LOB row has too few fields");
        }
        out.timestamp = parse_i64(fields[1]);
        out.asks.clear();
        out.bids.clear();
        out.asks.reserve(levels_);
        out.bids.reserve(levels_);
        for (std::size_t i = 0; i < levels_; ++i) {
            const std::size_t base = 2 + i * 4;
            out.asks.push_back(Level{parse_double(fields[base]), parse_double(fields[base + 1])});
            out.bids.push_back(Level{parse_double(fields[base + 2]), parse_double(fields[base + 3])});
        }
        return true;
    }

private:
    std::ifstream in_;
    std::string header_;
    std::size_t levels_ = 0;
};

inline double round_to_tick(double price, double tick_size) {
    return std::round(price / tick_size) * tick_size;
}

inline void apply_fill(const Fill& fill, double fee_bps, Metrics& metrics) {
    const double notional = fill.price * fill.quantity;
    const double fee = notional * fee_bps / 10000.0;
    if (fill.side == Side::Buy) {
        metrics.cash -= notional + fee;
        metrics.inventory += fill.quantity;
    } else {
        metrics.cash += notional - fee;
        metrics.inventory -= fill.quantity;
    }
    metrics.realized_fees += fee;
    metrics.turnover += notional;
    metrics.max_abs_inventory = std::max(metrics.max_abs_inventory, std::abs(metrics.inventory));
    ++metrics.fills;
}

}  // namespace hft
