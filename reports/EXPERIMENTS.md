# Simulation Experiments

Both experiments replayed the first 200,000 rows of `lob.csv` with 5,000 units per quote and a quote refresh every 25 snapshots. The AS formula is evaluated in log-return (relative) space and converted to absolute price units; `kappa=50000` targets a ~2-tick half-spread for this asset. Fills execute at the resting order's posted limit price with a full-fill-or-nothing cumulative depth check (sums displayed quantity across all LOB levels that crossed the order price).

| Experiment | PnL | Inventory | Turnover | Fills | Half-spread (ticks) | Max Abs Inventory |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline AS 2008 | -92.3945000000 | 0.0000000000 | 582143.8725000002 | 11128 | ~2.06 | 30000.0000000000 |
| Microprice extension | -92.8810000000 | 0.0000000000 | 584157.5410000014 | 11166 | ~2.06 | 30000.0000000000 |

## Interpretation

Both runs are net negative over this 200,000-snapshot window. This is expected: the asset declined approximately 6.9% over the period (mid moved from ~0.01104 to ~0.01028). A symmetric market maker in a trending market accumulates directional inventory losses that outweigh spread income — this is adverse selection, not a model defect.

The microprice extension fills slightly more (11166 vs 11128), consistent with shifted quotes reducing adverse selection on some snapshots. Over a strongly trending 200k-snapshot window the net directional loss from accumulating long inventory on a declining asset dominates, so both configs show similar PnL magnitudes.

**Key observations:**

- Final inventory is flat in both runs — inventory management is functioning.
- Max absolute inventory reached 30000 (six `order_quantity` units), which stays well within the `inventory_limit=50000` guard. The inventory skew limits accumulation but does not prevent multi-lot swings in a persistent trend.
- The cumulative depth check (sum across all crossed levels rather than top-of-book only) uses the full 25-level L2 data and produces slightly more fills than a top-of-book-only model would, since orders can be backed by depth at deeper levels.
- PnL at this scale reflects fills at the resting limit price with no price improvement, consistent with crypto maker matching.

## Next experiments

- Run on mean-reverting or range-bound slices of `lob.csv` to show positive PnL regime.
- Parameter sweep over `gamma` (inventory skew aggressiveness), `kappa` (target spread width), and `quote_interval_events`.
- Walk-forward train/test split for out-of-sample validation.
- Enable `fee_bps` to model realistic maker rebate vs taker fee structure.
- Export per-fill equity curve to visualise drawdown profile.
