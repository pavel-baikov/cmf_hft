# Reports

Run the backtester to generate performance reports:

```bash
make
./build/backtester configs/baseline_as2008.conf
./build/backtester configs/microprice_extension.conf
```

The generated markdown files contain PnL, inventory, turnover, fills, and final quote diagnostics.
