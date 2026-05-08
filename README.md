# C++ Market-Making Backtester

This repository contains a C++17 backtesting engine for replaying historical limit-order-book snapshots and evaluating Avellaneda-Stoikov market-making strategies.

## Deliverables

- Backtester source: `src/` and `include/`
- Sample dataset: provided `lob.csv`, `trades.csv`, and `MD.zip`
- Configs: `configs/default.conf`, `configs/baseline_as2008.conf`, `configs/microprice_extension.conf`
- Technical documentation: `docs/TECHNICAL.md`
- Performance reports: `reports/baseline_as2008.md`, `reports/microprice_extension.md`, `reports/EXPERIMENTS.md`

## Quick Start

```bash
make
./build/backtester configs/default.conf
```

Run the comparison experiments:

```bash
./build/backtester configs/baseline_as2008.conf
./build/backtester configs/microprice_extension.conf
```

## Implemented Features

- Streaming L2 snapshot replay from CSV.
- Limit order placement and cancellation.
- Crossing-based order execution model.
- Full-fill execution accounting.
- Metrics: mark-to-market PnL, cash, inventory, turnover, fees, fill count, order count, cancel count.
- Avellaneda-Stoikov 2008-style reservation-price market making.
- Microprice-enhanced extension using top-of-book imbalance.

## Execution Model

Execution occurs when the replayed market crosses a resting quote:

- Buy quote fills when `best_ask <= order_price`.
- Sell quote fills when `best_bid >= order_price`.

The engine fills the configured order size at the visible crossing price.

## Notes

The provided `trades.csv` is retained as part of the sample data package. The current execution model uses `lob.csv` because the exam assumption is based on market price crossing the order level. A natural next step is combining snapshots and prints to simulate queue depletion.
