#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict

if len(sys.argv) != 2:
    print(f"Usage: {sys.argv[0]} <csv>", file=sys.stderr)
    raise SystemExit(1)

path = sys.argv[1]
rows = []
with open(path, newline="", encoding="utf-8") as f:
    for row in csv.DictReader(f):
        rows.append(row)

if not rows:
    print("SHORTMSG_SUMMARY=FAIL reason=no_rows", file=sys.stderr)
    raise SystemExit(1)

if any(r["delivery_ok"] != "1" for r in rows):
    print("SHORTMSG_SUMMARY=FAIL reason=delivery", file=sys.stderr)
    raise SystemExit(1)

groups = defaultdict(list)
for r in rows:
    groups[(r["protocol"], int(r["payload_bytes"]))].append(r)

print("protocol payload_B nominal_hdr_ratio observed_overhead/useful median_goodput_Mbps")
for size in sorted({int(r["payload_bytes"]) for r in rows}):
    for proto in ("lwp", "tcp", "udp"):
        g = groups[(proto, size)]
        nominal = statistics.median(float(r["nominal_data_header_ratio"]) for r in g)
        observed = statistics.median(float(r["observed_ipv4_overhead_ratio"]) for r in g)
        goodput = statistics.median(float(r["goodput_mbps"]) for r in g)
        print(f"{proto:8s} {size:9d} {nominal:17.6f} {observed:24.6f} {goodput:20.6f}")

print(f"SHORTMSG_SUMMARY=PASS rows={len(rows)}")
