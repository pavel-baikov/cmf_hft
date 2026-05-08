# Simulation Experiments

Both experiments replayed the first 200,000 rows of `lob.csv` with 5,000 units per quote and a quote refresh every 25 snapshots.

| Experiment | PnL | Inventory | Turnover | Fills | Max Abs Inventory |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline AS 2008 | -16.2655000000 | 0.0000000000 | 572755.0874999993 | 10950 | 5000.0000000000 |
| Microprice extension | -16.3025000000 | 0.0000000000 | 571693.1774999991 | 10930 | 5000.0000000000 |

## Interpretation

On this short slice, the microprice extension reduced turnover and fills slightly but did not improve PnL under the current full-fill crossing model. The final inventory is flat in both runs, and max absolute inventory stayed at one quote size.

This result should be read as an engine validation run, not a calibrated production strategy. The next useful experiments are parameter sweeps over `gamma`, `kappa`, `quote_interval_events`, and `microprice_weight`, ideally with train/test time splits and transaction fees enabled.
