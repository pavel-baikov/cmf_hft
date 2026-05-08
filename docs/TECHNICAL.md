# Backtesting Engine Technical Documentation

## Scope

This project implements a C++17 historical replay backtester for a market-making strategy. It reads the provided `lob.csv` snapshot stream, maintains a simulated set of resting limit orders, applies cancel/replace strategy actions, models executions, and reports PnL, inventory, turnover, fills, and quote diagnostics.

The included dataset is the sample dataset for the exam:

- `lob.csv`: top-25 ask and bid levels by timestamp.
- `trades.csv`: trade prints supplied with the data package. The current engine uses snapshots for book state and crossing-based fills, matching the requested execution assumption.
- `MD.zip`: original archive containing the same CSV inputs.

## Build And Run

```bash
make
./build/backtester configs/default.conf
./build/backtester configs/baseline_as2008.conf
./build/backtester configs/microprice_extension.conf
```

Set `max_events=0` in a config to replay the whole `lob.csv` file.

## Engine Design

`CsvBookReader` streams L2 snapshots and extracts timestamp, ask levels, and bid levels. The backtester does not load the full dataset into memory.

`OrderManager` owns open simulated orders. The strategy periodically cancels all previous quotes and places a fresh bid and ask. This models a standard market-making quote refresh loop.

Execution uses the stated assumption:

- Buy order fills when replayed best ask `<= order.price`.
- Sell order fills when replayed best bid `>= order.price`.

The fill price is the visible crossing price, bounded by the order price. Full fills are used; partial fills are a straightforward extension because the book quantities are already parsed.

Metrics are updated on every fill:

- Cash: decreases for buys and increases for sells.
- Inventory: increases for buys and decreases for sells.
- Turnover: sum of filled notional.
- Fees: optional `fee_bps`.
- Mark-to-market PnL: `cash + inventory * last_mid`.

## Strategy Model

The baseline strategy follows Avellaneda-Stoikov (2008) market making:

- Estimate rolling volatility σ from per-event log mid-price returns.
- Compute a reservation price that penalizes inventory exposure.
- Compute a half-spread from risk and liquidity terms.
- Quote bid and ask symmetrically around the reservation price.

All intermediate formula terms are computed in **log-return (relative, dimensionless) space** and converted to absolute price units at the end by multiplying by the reference price. σ is the per-event log-return standard deviation; γ and κ are dimensionless parameters; `horizon_seconds` is the number of forward events T in the AS model.

```text
reference   = mid                                   (baseline)
reservation = reference × (1 − q × γ × σ² × T)     (inventory skew, price units)

risk_rel    = γ × σ² × T                            (relative)
liq_rel     = (2/γ) × ln(1 + γ/κ)                  (relative)
half_spread = max(market_spread/2,
                  0.5 × (risk_rel + liq_rel) × reference)   (absolute, price units)
```

With the provided dataset (mid ~0.0103, σ ~9×10⁻⁵ per event, γ=0.08, κ=50000, T=60):
- `liq_rel ≈ 4×10⁻⁵`, `half_spread ≈ 2×10⁻⁷` (≈ 2 ticks)
- Per `order_quantity` (5000 units) of inventory, the absolute reservation skew is
  `q × γ × σ² × T × reference ≈ 5000 × 0.08 × 8.1×10⁻⁹ × 60 × 0.0103 ≈ 2×10⁻⁶` (≈ **20 ticks**),
  biasing quotes to unwind.

The optional microprice-blend extension replaces the pure mid-price reference with an
imbalance-weighted blend (this is a custom modification, not from a specific published paper):

```text
microprice = (ask × bid_size + bid × ask_size) / (bid_size + ask_size)
reference  = (1 − w) × mid + w × microprice
```

When bid size dominates ask size, microprice moves toward the ask (upward pressure), shifting both reservation and quotes in that direction to reduce adverse selection.

## Configuration

Configs are simple `key=value` files in `configs/`.

Important parameters:

- `quote_interval_events`: number of snapshots between quote refreshes.
- `order_quantity`: simulated quote size.
- `gamma`: dimensionless inventory risk aversion; controls reservation skew per unit inventory.
- `kappa`: dimensionless order arrival rate; primary control of quoted spread width. Higher kappa → narrower spread. Use the approximation `kappa ≈ gamma / (2 × target_relative_half_spread)` to calibrate.
- `volatility_window`: rolling return window in events.
- `horizon_seconds`: number of forward events T in the AS model (naming is historical; units are events).
- `microprice_weight`: blend weight for the extension (0 = pure mid, 1 = pure microprice).
- `inventory_limit`: disables one side when inventory breaches the limit.

## Roadmap

- Add queue-position and partial-fill modeling using displayed depth.
- Consume `trades.csv` to model trade-through and maker queue depletion.
- Add latency, order acknowledgements, and cancel acknowledgements.
- Calibrate `gamma`, `kappa`, and quote refresh interval with walk-forward splits.
- Add benchmark strategies and parameter sweeps.
- Export per-fill and per-snapshot equity curves for deeper analysis.
