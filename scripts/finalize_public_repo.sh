#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

# Preserve the validated M3 short-message result as the public canonical result.
if [ -f results/short_message_benchmark_m3.csv ]; then
    cp results/short_message_benchmark_m3.csv results/short_message_benchmark.csv
fi
rm -f results/short_message_benchmark_m3.csv

# Preserve the validated loss-sweep CSV/summary under stable public names.
if [ -d results/m3_loss_sweep ]; then
    mkdir -p results/loss_sweep
    [ -f results/m3_loss_sweep/loss_sweep.csv ] && cp results/m3_loss_sweep/loss_sweep.csv results/loss_sweep/loss_sweep.csv
    [ -f results/m3_loss_sweep/loss_sweep_summary.txt ] && cp results/m3_loss_sweep/loss_sweep_summary.txt results/loss_sweep/loss_sweep_summary.txt
fi

# Remove build products and transient/debug artifacts.
rm -rf build results/.shortmsg_tmp results/m3_loss5 results/m3_loss_sweep
find results -type f -name '*.log' -delete 2>/dev/null || true
rm -f results/receiver.log results/sender.log results/receiver_loss5.log results/sender_loss5.log
rm -f LWP_*.tar.gz LWP_*.tar.gz.sha256 README_PATCH.txt
rm -rf scripts/__pycache__

# Remove development-stage documents/scripts superseded by the public names.
rm -f docs/m3-ack-aggregation.md docs/m3-loss-validation.md docs/m3-loss-sweep.md docs/short-message-evaluation.md
rm -f scripts/compare_m2_m3.py scripts/run_m3_loss5_validation.sh scripts/run_m3_loss_sweep.sh scripts/summarize_m3_loss_sweep.py

chmod +x scripts/*.sh scripts/*.py 2>/dev/null || true

echo "LWP_PUBLIC_FINALIZE=PASS"
echo "Kept results:"
find results -maxdepth 2 -type f | sort
