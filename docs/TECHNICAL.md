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

The baseline strategy follows Avellaneda-Stoikov style market making:

- Estimate rolling volatility from log mid-price returns.
- Compute a reservation price that penalizes inventory.
- Compute a half spread from risk and liquidity terms.
- Quote bid and ask around the reservation price.

The baseline reference price is the mid-price:

```text
reference = mid
reservation = reference - inventory * gamma * sigma^2 * horizon
```

The extension replaces the pure mid-price reference with a microprice blend:

```text
microprice = (ask * bid_size + bid * ask_size) / (bid_size + ask_size)
reference = (1 - w) * mid + w * microprice
```

This adds top-of-book imbalance information to the quoted center. When bid size dominates ask size, microprice moves toward the ask, and vice versa.

## Configuration

Configs are simple `key=value` files in `configs/`.

Important parameters:

- `quote_interval_events`: number of snapshots between quote refreshes.
- `order_quantity`: simulated quote size.
- `gamma`: inventory risk aversion.
- `kappa`: liquidity shape parameter in the spread formula.
- `volatility_window`: rolling return window.
- `horizon_seconds`: risk horizon scalar.
- `microprice_weight`: blend weight for the extension.
- `inventory_limit`: disables one side when inventory breaches the limit.

## Roadmap

- Add queue-position and partial-fill modeling using displayed depth.
- Consume `trades.csv` to model trade-through and maker queue depletion.
- Add latency, order acknowledgements, and cancel acknowledgements.
- Calibrate `gamma`, `kappa`, and quote refresh interval with walk-forward splits.
- Add benchmark strategies and parameter sweeps.
- Export per-fill and per-snapshot equity curves for deeper analysis.
