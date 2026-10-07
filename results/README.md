# Results

The public repository keeps compact, reproducible result artifacts rather than transient per-run logs.

- `short_message_benchmark.csv`: LWP/TCP/UDP short-message benchmark, 2,000 messages, payloads 8--512 B, 3 repeats.
- `loss_sweep/loss_sweep.csv`: LWP robustness sweep at 0%, 0.1%, 1%, and 5% forward loss, 3 repeats.
- `loss_sweep/loss_sweep_summary.txt`: median summary generated from the loss-sweep CSV.

Per-run sender/receiver/meter logs are intentionally excluded from the public repository; the scripts regenerate them when experiments are rerun.
