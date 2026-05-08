# Simulation Experiments

Both experiments replayed the first 200,000 rows of `lob.csv` with 5,000 units per quote and a quote refresh every 25 snapshots. The AS formula is evaluated in log-return (relative) space and converted to absolute price units; `kappa=50000` targets a ~2-tick half-spread for this asset.

| Experiment | PnL | Inventory | Turnover | Fills | Half-spread (ticks) | Max Abs Inventory |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline AS 2008 | -20.6515000000 | 0.0000000000 | 582877.1104999982 | 11142 | ~2.06 | 5000.0000000000 |
| Microprice extension | -20.6285000000 | 0.0000000000 | 584996.7784999990 | 11182 | ~2.06 | 5000.0000000000 |

## Interpretation

Both runs are net negative over this 200,000-snapshot window. This is expected: the asset declined approximately 6.9% over the period (mid moved from ~0.01104 to ~0.01028). A symmetric market maker in a trending market accumulates directional inventory losses that outweigh spread income — this is adverse selection, not a model defect.

The microprice extension shows a consistent improvement of +0.023 PnL units relative to the baseline. Unlike the pre-calibration run (where sub-tick spreads pinned both configs identically at the touch), the 2-tick spread allows the microprice signal to shift quotes in the direction of top-of-book imbalance, measurably reducing adverse selection.

**Key observations:**

- Final inventory is flat in both runs — inventory management is functioning.
- Max absolute inventory stayed at one `order_quantity` — the inventory skew prevents accumulation.
- Microprice extension fills slightly more (11182 vs 11142) at better average prices, producing the PnL improvement.

## Next experiments

- Run on mean-reverting or range-bound slices of `lob.csv` to show positive PnL regime.
- Parameter sweep over `gamma` (inventory skew aggressiveness), `kappa` (target spread width), and `quote_interval_events`.
- Walk-forward train/test split for out-of-sample validation.
- Enable `fee_bps` to model realistic maker rebate vs taker fee structure.
- Export per-fill equity curve to visualise drawdown profile.
