#pragma once

#include "csv.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hft {

enum class Side { Buy, Sell };

struct Level {
    double price    = 0.0;
    double quantity = 0.0;
};

struct BookSnapshot {
    std::int64_t        timestamp = 0;
    std::vector<Level>  asks;
    std::vector<Level>  bids;

    double best_ask() const {
        return asks.empty() ? std::numeric_limits<double>::quiet_NaN() : asks.front().price;
    }
    double best_bid() const {
        return bids.empty() ? std::numeric_limits<double>::quiet_NaN() : bids.front().price;
    }
    double mid() const { return 0.5 * (best_bid() + best_ask()); }
    double spread() const { return best_ask() - best_bid(); }
    double microprice() const {
        if (asks.empty() || bids.empty()) return mid();
        const double bid_qty = std::max(0.0, bids.front().quantity);
        const double ask_qty = std::max(0.0, asks.front().quantity);
        const double denom   = bid_qty + ask_qty;
        if (denom <= 0.0) return mid();
        return (best_ask() * bid_qty + best_bid() * ask_qty) / denom;
    }
};

struct Order {
    int          id        = 0;
    Side         side      = Side::Buy;
    double       price     = 0.0;
    double       quantity  = 0.0;
    std::int64_t placed_ts = 0;
};

struct Fill {
    int          order_id  = 0;
    Side         side      = Side::Buy;
    double       price     = 0.0;
    double       quantity  = 0.0;
    std::int64_t timestamp = 0;
};

struct Metrics {
    double      cash              = 0.0;
    double      inventory         = 0.0;
    double      turnover          = 0.0;
    double      realized_fees     = 0.0;
    double      last_mid          = 0.0;
    double      max_abs_inventory = 0.0;
    std::size_t fills             = 0;
    std::size_t snapshots         = 0;
    std::size_t orders_placed     = 0;
    std::size_t cancels           = 0;

    double mark_to_market_pnl() const { return cash + inventory * last_mid; }
};

struct EngineConfig {
    std::string lob_path                = "lob.csv";
    std::string report_path             = "reports/performance.md";
    std::size_t levels                  = 25;
    std::size_t max_events              = 200000;
    std::size_t warmup_events           = 1000;
    std::size_t quote_interval_events   = 25;
    double      tick_size               = 0.0000001;
    double      order_quantity          = 5000.0;
    double      fee_bps                 = 0.0;
    double      gamma                   = 0.08;
    double      kappa                   = 50000.0;
    double      volatility_window       = 500.0;
    double      horizon_seconds         = 60.0;
    double      microprice_weight       = 0.65;
    double      inventory_limit         = 50000.0;
    bool        use_microprice_extension = true;
};

inline std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline bool parse_bool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

inline EngineConfig load_config(const std::string& path) {
    EngineConfig cfg;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open config: " + path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error("invalid config line: " + line);
        const std::string key   = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if      (key == "lob_path")                cfg.lob_path                = value;
        else if (key == "report_path")             cfg.report_path             = value;
        else if (key == "levels")                  cfg.levels                  = static_cast<std::size_t>(std::stoull(value));
        else if (key == "max_events")              cfg.max_events              = static_cast<std::size_t>(std::stoull(value));
        else if (key == "warmup_events")           cfg.warmup_events           = static_cast<std::size_t>(std::stoull(value));
        else if (key == "quote_interval_events")   cfg.quote_interval_events   = static_cast<std::size_t>(std::stoull(value));
        else if (key == "tick_size")               cfg.tick_size               = std::stod(value);
        else if (key == "order_quantity")          cfg.order_quantity          = std::stod(value);
        else if (key == "fee_bps")                 cfg.fee_bps                 = std::stod(value);
        else if (key == "gamma")                   cfg.gamma                   = std::stod(value);
        else if (key == "kappa")                   cfg.kappa                   = std::stod(value);
        else if (key == "volatility_window")       cfg.volatility_window       = std::stod(value);
        else if (key == "horizon_seconds")         cfg.horizon_seconds         = std::stod(value);
        else if (key == "microprice_weight")       cfg.microprice_weight       = std::stod(value);
        else if (key == "inventory_limit")         cfg.inventory_limit         = std::stod(value);
        else if (key == "use_microprice_extension") cfg.use_microprice_extension = parse_bool(value);
        else throw std::runtime_error("unknown config key: " + key);
    }
    return cfg;
}

inline void validate_config(const EngineConfig& cfg) {
    if (cfg.tick_size <= 0.0)
        throw std::runtime_error("tick_size must be > 0");
    if (cfg.gamma <= 0.0)
        throw std::runtime_error("gamma must be > 0");
    if (cfg.kappa <= 0.0)
        throw std::runtime_error("kappa must be > 0");
    if (cfg.order_quantity <= 0.0)
        throw std::runtime_error("order_quantity must be > 0");
    if (cfg.inventory_limit <= 0.0)
        throw std::runtime_error("inventory_limit must be > 0");
    if (cfg.microprice_weight < 0.0 || cfg.microprice_weight > 1.0)
        throw std::runtime_error("microprice_weight must be in [0, 1]");
    if (cfg.volatility_window < 2.0)
        throw std::runtime_error("volatility_window must be >= 2");
    if (cfg.horizon_seconds <= 0.0)
        throw std::runtime_error("horizon_seconds must be > 0");
    if (cfg.quote_interval_events == 0)
        throw std::runtime_error("quote_interval_events must be > 0");
    if (cfg.levels == 0)
        throw std::runtime_error("levels must be > 0");
    if (2 + cfg.levels * 4 > MAX_CSV_FIELDS)
        throw std::runtime_error("levels exceeds MAX_CSV_FIELDS; increase MAX_CSV_FIELDS");
}

// Maximum simultaneously open orders; the strategy places 2 per cycle.
static constexpr std::size_t MAX_OPEN_ORDERS = 8;
// Stack buffer size for fills produced in a single matching pass.
static constexpr std::size_t MAX_FILLS_PER_TICK = MAX_OPEN_ORDERS;

class OrderManager {
    struct Slot {
        Order order{};
        bool  used = false;
    };

public:
    int place(Side side, double price, double quantity, std::int64_t ts) {
        for (auto& s : slots_) {
            if (!s.used) {
                s.order = {++next_id_, side, price, quantity, ts};
                s.used  = true;
                return s.order.id;
            }
        }
        throw std::runtime_error("OrderManager: slot overflow (increase MAX_OPEN_ORDERS)");
    }

    std::size_t cancel_all() {
        std::size_t count = 0;
        for (auto& s : slots_) {
            if (s.used) { s.used = false; ++count; }
        }
        return count;
    }

    // Writes fills into out[0..cap). Returns number of fills. O(MAX_OPEN_ORDERS), zero allocs.
    // Throws if more fills occur than cap — caller must size cap >= MAX_OPEN_ORDERS.
    [[nodiscard]] std::size_t match_crossing_orders(const BookSnapshot& book,
                                                    Fill* out, std::size_t cap) {
        std::size_t n = 0;
        for (auto& s : slots_) {
            if (!s.used) continue;
            const auto& o      = s.order;
            const bool crosses = o.side == Side::Buy
                ? book.best_ask() <= o.price
                : book.best_bid() >= o.price;
            if (!crosses) continue;
            // Full-fill-or-nothing cumulative depth check. Sum displayed quantity across
            // every level that has crossed through the order price (asks ≤ order_price for
            // buys; bids ≥ order_price for sells). If the total is below the order size the
            // order stays open and is re-evaluated on the next snapshot or cancelled at the
            // next refresh. Using cumulative depth rather than top-of-book-only is the
            // natural use of the 25 parsed levels for a realistic full-fill model.
            double avail = 0.0;
            if (o.side == Side::Buy) {
                for (const auto& lvl : book.asks) {
                    if (lvl.price > o.price) break;
                    avail += lvl.quantity;
                    if (avail >= o.quantity) break;
                }
            } else {
                for (const auto& lvl : book.bids) {
                    if (lvl.price < o.price) break;
                    avail += lvl.quantity;
                    if (avail >= o.quantity) break;
                }
            }
            if (avail < o.quantity) continue;
            // Record fill before clearing the slot. If the buffer is exhausted it is a
            // programming error (cap must be >= MAX_OPEN_ORDERS); throw rather than
            // silently dropping a fill and corrupting cash/inventory accounting.
            if (n >= cap) throw std::runtime_error("match_crossing_orders: fill buffer overflow");
            // Resting limit order fills at its own posted price (standard crypto maker
            // matching). The aggressor receives price improvement if the market gapped;
            // the maker always transacts at their limit price.
            out[n++] = {o.id, o.side, o.price, o.quantity, book.timestamp};
            s.used = false;
        }
        return n;
    }

private:
    int  next_id_ = 0;
    std::array<Slot, MAX_OPEN_ORDERS> slots_{};
};

// Streams L2 snapshots from CSV. Reuses a single line buffer and a fixed-size field array;
// zero heap allocations after construction.
class CsvBookReader {
    static constexpr std::size_t IO_BUF_BYTES = 1u << 20; // 1 MB read buffer

public:
    explicit CsvBookReader(const std::string& path, std::size_t levels)
        : levels_(levels) {
        // Allocate the read buffer and install it BEFORE opening the file so that
        // pubsetbuf is called on a stream with no file association yet (well-defined).
        io_buf_.resize(IO_BUF_BYTES);
        in_.rdbuf()->pubsetbuf(io_buf_.data(), static_cast<std::streamsize>(IO_BUF_BYTES));
        in_.open(path);
        if (!in_) throw std::runtime_error("cannot open LOB file: " + path);
        line_.reserve(2048);
        std::getline(in_, line_); // consume header
    }

    // Pre-sizes the BookSnapshot vectors to avoid per-row reallocation.
    // next() also auto-sizes defensively, but calling this before the loop is preferred.
    void prepare(BookSnapshot& snap) const {
        snap.asks.resize(levels_);
        snap.bids.resize(levels_);
    }

    // Reads the next row into out. Returns false at EOF.
    // Writing into pre-sized vectors: O(levels), zero allocs.
    bool next(BookSnapshot& out) {
        if (!std::getline(in_, line_)) return false;
        // Defensive resize: handles the case where prepare() was not called.
        if (out.asks.size() != levels_) {
            out.asks.resize(levels_);
            out.bids.resize(levels_);
        }
        const auto n = split_csv_line(std::string_view(line_), fields_);
        const std::size_t needed = 2 + levels_ * 4;
        if (n < needed) throw std::runtime_error("LOB row has too few fields");
        out.timestamp = parse_i64(fields_[1]);
        for (std::size_t i = 0; i < levels_; ++i) {
            const std::size_t base = 2 + i * 4;
            out.asks[i] = {parse_double(fields_[base]),     parse_double(fields_[base + 1])};
            out.bids[i] = {parse_double(fields_[base + 2]), parse_double(fields_[base + 3])};
        }
        return true;
    }

private:
    // io_buf_ MUST be declared before in_: members destruct in reverse declaration order,
    // so io_buf_ will be freed after in_ closes — preventing a dangling pointer in
    // ~ifstream when it accesses the buffer installed via pubsetbuf.
    std::vector<char> io_buf_;
    std::ifstream in_;
    std::string line_;
    std::array<std::string_view, MAX_CSV_FIELDS> fields_;
    std::size_t levels_ = 0;
};

inline double round_to_tick(double price, double tick_size) {
    return std::round(price / tick_size) * tick_size;
}

inline void apply_fill(const Fill& fill, double fee_bps, Metrics& metrics) {
    const double notional = fill.price * fill.quantity;
    const double fee      = notional * fee_bps / 10000.0;
    if (fill.side == Side::Buy) {
        metrics.cash      -= notional + fee;
        metrics.inventory += fill.quantity;
    } else {
        metrics.cash      += notional - fee;
        metrics.inventory -= fill.quantity;
    }
    metrics.realized_fees     += fee;
    metrics.turnover          += notional;
    metrics.max_abs_inventory  = std::max(metrics.max_abs_inventory, std::abs(metrics.inventory));
    ++metrics.fills;
}

}  // namespace hft
