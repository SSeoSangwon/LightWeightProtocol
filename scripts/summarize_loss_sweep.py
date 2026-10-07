#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict

if len(sys.argv) != 2:
    print("usage: summarize_loss_sweep.py <csv>", file=sys.stderr)
    sys.exit(2)

path = sys.argv[1]
groups = defaultdict(list)
with open(path, newline="", encoding="utf-8") as f:
    for row in csv.DictReader(f):
        groups[float(row["loss_percent"])].append(row)

expected = [0.0, 0.1, 1.0, 5.0]
if sorted(groups) != expected:
    print(f"LOSS_SWEEP_SUMMARY=FAIL reason=loss_set observed={sorted(groups)}")
    sys.exit(1)

print("loss_pct runs delivery_rate median_retx median_timeouts median_ack_requests median_acks_sent median_goodput_Mbps")
for loss in expected:
    rows = groups[loss]
    delivery_rate = sum(int(r["delivered"]) == int(r["messages"]) for r in rows) / len(rows)
    retr = [int(r["retransmissions"]) for r in rows]
    timeouts = [int(r["timeouts"]) for r in rows]
    ack_req = [int(r["ack_requests"]) for r in rows]
    acks = [int(r["acknowledgments_sent"]) for r in rows]
    goodput = [float(r["goodput_mbps"]) for r in rows]
    print(
        f"{loss:7g} {len(rows):4d} {delivery_rate:13.3f} "
        f"{statistics.median(retr):11.1f} {statistics.median(timeouts):15.1f} "
        f"{statistics.median(ack_req):19.1f} {statistics.median(acks):16.1f} "
        f"{statistics.median(goodput):19.6f}"
    )

# Sanity checks only where loss is large enough that recovery activity should be observable.
for loss in (1.0, 5.0):
    rows = groups[loss]
    if sum(int(r["retransmissions"]) for r in rows) <= 0:
        print(f"LOSS_SWEEP_SUMMARY=FAIL reason=no_retransmission loss={loss:g}")
        sys.exit(1)

if any(int(r["corrupted"]) != 0 for rows in groups.values() for r in rows):
    print("LOSS_SWEEP_SUMMARY=FAIL reason=corruption")
    sys.exit(1)

print("LOSS_SWEEP_SUMMARY=PASS")
