#include "backtester.hpp"
#include "strategy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

// g_pass and g_fail count individual CHECK calls, not test functions.
static void check(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++g_pass;
    } else {
        std::fprintf(stderr, "  FAIL @ %s:%d: %s\n", file, line, expr);
        ++g_fail;
    }
}

#define CHECK(expr)            check((expr),         #expr,          __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps)  check(std::abs((a)-(b)) < (eps), \
                                     #a " ≈ " #b, __FILE__, __LINE__)

static void begin_test(const char* name) {
    std::printf("  %s\n", name);
}
#define TEST(name) begin_test(name);

static hft::BookSnapshot make_book(double bid, double bid_qty,
                                   double ask, double ask_qty,
                                   std::int64_t ts = 1000) {
    hft::BookSnapshot b;
    b.timestamp = ts;
    b.bids = {{bid, bid_qty}};
    b.asks = {{ask, ask_qty}};
    return b;
}

// ── apply_fill accounting ──────────────────────────────────────────────────

static void test_apply_fill_buy() {
    TEST("apply_fill buy: cash decreases, inventory increases");
    hft::Metrics m{};
    hft::Fill f{1, hft::Side::Buy, 0.0100, 5000.0, 1000};
    hft::apply_fill(f, 0.0, m);
    CHECK_NEAR(m.cash,               -50.0, 1e-9);
    CHECK_NEAR(m.inventory,         5000.0, 1e-9);
    CHECK_NEAR(m.turnover,            50.0, 1e-9);
    CHECK(m.fills == 1);
    CHECK_NEAR(m.max_abs_inventory, 5000.0, 1e-9);
}

static void test_apply_fill_sell() {
    TEST("apply_fill sell: cash increases, inventory decreases");
    hft::Metrics m{};
    m.inventory = 5000.0;
    hft::Fill f{1, hft::Side::Sell, 0.0105, 5000.0, 1000};
    hft::apply_fill(f, 0.0, m);
    CHECK_NEAR(m.cash,      52.5, 1e-9);
    CHECK_NEAR(m.inventory,  0.0, 1e-9);
    CHECK_NEAR(m.turnover,  52.5, 1e-9);
    CHECK(m.fills == 1);
}

static void test_apply_fill_fee() {
    TEST("apply_fill fee: deducted from buy side correctly");
    hft::Metrics m{};
    hft::Fill f{1, hft::Side::Buy, 1.0, 1.0, 1000};
    hft::apply_fill(f, 100.0, m); // 1% fee (100 bps)
    CHECK_NEAR(m.cash,          -1.01, 1e-9);
    CHECK_NEAR(m.realized_fees,  0.01, 1e-9);
}

static void test_apply_fill_max_abs_inventory() {
    TEST("apply_fill max_abs_inventory tracks peak, not current");
    hft::Metrics m{};
    hft::Fill buy{1, hft::Side::Buy, 1.0, 3000.0, 1000};
    hft::apply_fill(buy, 0.0, m);
    CHECK_NEAR(m.max_abs_inventory, 3000.0, 1e-9);
    hft::Fill sell{2, hft::Side::Sell, 1.0, 5000.0, 1001};
    hft::apply_fill(sell, 0.0, m);
    // inventory is now -2000; peak was 3000
    CHECK_NEAR(m.max_abs_inventory, 3000.0, 1e-9);
}

// ── OrderManager fill price and depth check ────────────────────────────────

static void test_fill_at_order_price_buy() {
    TEST("buy order fills at posted limit price, not at best ask");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0103, 100.0, 1000);
    auto book = make_book(0.0098, 200.0, 0.0100, 200.0); // ask crosses buy @ 0.0103
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 1);
    CHECK_NEAR(buf[0].price, 0.0103, 1e-10); // posted price, not 0.0100
}

static void test_fill_at_order_price_sell() {
    TEST("sell order fills at posted limit price, not at best bid");
    hft::OrderManager om;
    om.place(hft::Side::Sell, 0.0099, 100.0, 1000);
    auto book = make_book(0.0102, 200.0, 0.0104, 200.0); // bid crosses sell @ 0.0099
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 1);
    CHECK_NEAR(buf[0].price, 0.0099, 1e-10); // posted price, not 0.0102
}

static void test_depth_check_skips_undersized_level() {
    TEST("depth check: order skipped when top-of-book qty < order qty");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0103, 500.0, 1000);
    auto book = make_book(0.0098, 200.0, 0.0100, 100.0); // only 100 qty, need 500
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 0);
}

static void test_depth_check_fills_when_sufficient() {
    TEST("depth check: order fills when top-of-book qty >= order qty");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0103, 100.0, 1000);
    auto book = make_book(0.0098, 200.0, 0.0100, 100.0); // exactly 100, should fill
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 1);
}

static void test_depth_check_cumulative_fills() {
    TEST("depth check: cumulative depth across crossed levels satisfies order");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0105, 300.0, 1000);
    // Two ask levels cross our bid (0.0100 qty=100 and 0.0103 qty=200); cumulative=300 == need
    hft::BookSnapshot book;
    book.timestamp = 1000;
    book.bids = {{0.0095, 500.0}};
    book.asks = {{0.0100, 100.0}, {0.0103, 200.0}, {0.0107, 500.0}}; // 0.0107 is above order
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 1);
}

static void test_depth_check_cumulative_insufficient() {
    TEST("depth check: cumulative depth across crossed levels below order size, skip");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0105, 400.0, 1000);
    // Same two levels cross (100+200=300), but need 400 — should skip
    hft::BookSnapshot book;
    book.timestamp = 1000;
    book.bids = {{0.0095, 500.0}};
    book.asks = {{0.0100, 100.0}, {0.0103, 200.0}, {0.0107, 500.0}};
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 0);
}

static void test_no_fill_when_not_crossing() {
    TEST("no fill when order price does not cross market");
    hft::OrderManager om;
    om.place(hft::Side::Buy, 0.0099, 100.0, 1000);
    auto book = make_book(0.0098, 200.0, 0.0100, 200.0); // ask=0.0100 > buy=0.0099
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 0);
}

static void test_cancel_all() {
    TEST("cancel_all removes all open orders");
    hft::OrderManager om;
    om.place(hft::Side::Buy,  0.010, 100.0, 1000);
    om.place(hft::Side::Sell, 0.011, 100.0, 1000);
    auto cancelled = om.cancel_all();
    CHECK(cancelled == 2);
    auto book = make_book(0.009, 500.0, 0.009, 500.0);
    hft::Fill buf[8];
    auto n = om.match_crossing_orders(book, buf, 8);
    CHECK(n == 0);
}

// ── Microprice formula ─────────────────────────────────────────────────────

static void test_microprice_formula() {
    TEST("microprice: (ask*bid_qty + bid*ask_qty) / (bid_qty + ask_qty)");
    // bid=0.010 @ 300, ask=0.011 @ 100
    // mp = (0.011*300 + 0.010*100) / 400 = 4.3/400 = 0.01075
    hft::BookSnapshot b;
    b.bids = {{0.010, 300.0}};
    b.asks = {{0.011, 100.0}};
    CHECK_NEAR(b.microprice(), 0.01075, 1e-10);
}

static void test_microprice_equal_sizes() {
    TEST("microprice equals mid when bid_qty == ask_qty");
    hft::BookSnapshot b;
    b.bids = {{0.010, 200.0}};
    b.asks = {{0.012, 200.0}};
    CHECK_NEAR(b.microprice(), b.mid(), 1e-10);
}

// ── AS quote formula ───────────────────────────────────────────────────────

static void test_as_quote_zero_inventory() {
    TEST("AS quote: quotes do not cross market when inventory=0 and sigma=0");
    hft::EngineConfig cfg;
    cfg.tick_size       = 0.0000001;
    cfg.gamma           = 0.08;
    cfg.kappa           = 50000.0;
    cfg.horizon_seconds = 60.0;
    cfg.volatility_window = 10;
    cfg.use_microprice_extension = false;
    cfg.microprice_weight = 0.0;
    hft::AvellanedaStoikovStrategy strat(cfg);
    auto book = make_book(0.0099, 100.0, 0.0101, 100.0);
    for (int i = 0; i < 20; ++i) strat.observe(book);
    auto q = strat.quote(book, 0.0);
    CHECK(q.bid <= book.best_bid() + 1e-12);
    CHECK(q.ask >= book.best_ask() - 1e-12);
}

static void test_as_quote_inventory_skews_reservation() {
    TEST("AS quote: long inventory skews reservation_price downward");
    hft::EngineConfig cfg;
    cfg.tick_size       = 0.0000001;
    cfg.gamma           = 0.08;
    cfg.kappa           = 50000.0;
    cfg.horizon_seconds = 60.0;
    cfg.volatility_window = 100;
    cfg.use_microprice_extension = false;
    cfg.microprice_weight = 0.0;
    hft::AvellanedaStoikovStrategy strat(cfg);
    // Use tiny price variations (0.01% per step) so sigma stays realistic and the
    // inventory skew shifts the reservation without driving it negative.
    double base = 0.01000;
    for (int i = 0; i < 50; ++i) {
        double p = base + (i % 2 == 0 ? 1e-6 : -1e-6);
        strat.observe(make_book(p - 0.0001, 100.0, p + 0.0001, 100.0));
    }
    auto book = make_book(0.0099, 100.0, 0.0101, 100.0);
    auto q_flat = strat.quote(book, 0.0);
    auto q_long = strat.quote(book, 1.0); // small inventory to keep skew measurable
    // The reservation price must be strictly lower for long inventory (q>0) vs flat (q=0).
    CHECK(q_long.reservation_price < q_flat.reservation_price);
}

// ── RollingVolatility ──────────────────────────────────────────────────────

static void test_rolling_vol_constant_price() {
    TEST("RollingVolatility: sigma=0 for constant price series");
    hft::RollingVolatility rv(10);
    for (int i = 0; i < 20; ++i) rv.update(0.0100);
    CHECK_NEAR(rv.sigma(), 0.0, 1e-15);
}

static void test_rolling_vol_two_samples() {
    TEST("RollingVolatility: non-zero sigma after two log-return samples");
    hft::RollingVolatility rv(10);
    // Three updates produce two log-returns; count_ must reach 2 before sigma() > 0.
    rv.update(0.0100);
    rv.update(0.0101);
    rv.update(0.0100);
    CHECK(rv.sigma() > 0.0);
}

static void test_rolling_vol_eviction() {
    TEST("RollingVolatility: evicted old samples, sigma returns to 0 on flat tail");
    hft::RollingVolatility rv(4);
    // Introduce variance then replace with flat prices
    for (int i = 0; i < 4; ++i) rv.update(1.0 + 0.001 * (i % 2)); // alternating
    // Overwrite window with identical values
    for (int i = 0; i < 5; ++i) rv.update(1.0);
    CHECK_NEAR(rv.sigma(), 0.0, 1e-15);
}

// ── round_to_tick ──────────────────────────────────────────────────────────

static void test_round_to_tick() {
    TEST("round_to_tick: rounds price to nearest tick boundary");
    CHECK_NEAR(hft::round_to_tick(0.01034, 0.0001), 0.0103, 1e-10);
    CHECK_NEAR(hft::round_to_tick(0.01035, 0.0001), 0.0104, 1e-10);
    CHECK_NEAR(hft::round_to_tick(0.0,     0.0001),  0.0,   1e-10);
}

int main() {
    std::printf("Running engine unit tests\n\n");

    test_apply_fill_buy();
    test_apply_fill_sell();
    test_apply_fill_fee();
    test_apply_fill_max_abs_inventory();

    test_fill_at_order_price_buy();
    test_fill_at_order_price_sell();
    test_depth_check_skips_undersized_level();
    test_depth_check_fills_when_sufficient();
    test_depth_check_cumulative_fills();
    test_depth_check_cumulative_insufficient();
    test_no_fill_when_not_crossing();
    test_cancel_all();

    test_microprice_formula();
    test_microprice_equal_sizes();

    test_as_quote_zero_inventory();
    test_as_quote_inventory_skews_reservation();

    test_rolling_vol_constant_price();
    test_rolling_vol_two_samples();
    test_rolling_vol_eviction();

    test_round_to_tick();

    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
