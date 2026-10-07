# LightWeightProtocol

**Spring 2024 · SKKU URP**

LWP (Light-Weight Protocol) is an experimental reliable transport prototype for short-message communication in controlled, pre-configured networks. The project starts from a simple observation: when application messages are small, transport metadata and control traffic can become a large fraction of the bytes sent on the network.

LWP keeps IPv4 addressing and routing unchanged, but moves stable transport-level connection information out of the per-packet data path. A pre-registered 16-bit Connection ID identifies the communicating context, while only the state needed for reliable delivery remains in the LWP header.

This repository contains a clean reimplementation and reproducible validation of the original Spring 2024 URP concept. Historical performance claims are not reused as experimental results.

## Design

LWP is implemented in Linux userspace over IPv4 raw sockets. It does **not** modify the Linux kernel.

The wire header is explicitly serialized and fixed at 11 bytes:

| Field | Size |
|---|---:|
| Version | 1 B |
| Flags | 1 B |
| Connection ID | 2 B |
| Sequence | 2 B |
| Acknowledgment | 2 B |
| Advertised Window | 1 B |
| Checksum | 2 B |
| **Total** | **11 B** |

The current implementation provides:

- pre-registered Connection ID instead of per-packet source/destination transport identifiers;
- explicit serialization and network byte order;
- sequence numbers and cumulative acknowledgments;
- fixed sliding-window transmission;
- window-level ACK aggregation using `ACK_REQ`;
- timeout-driven Go-Back-N retransmission;
- duplicate/out-of-order detection;
- payload/header integrity checking;
- FIN/FIN-ACK termination;
- IPv4 IHL-aware receive parsing.

See [`docs/protocol.md`](docs/protocol.md) for the protocol details.

## Build and Correctness Test

Requirements: Linux, GCC, Make, `iproute2`, Python 3, and `sudo` privileges for raw sockets/network namespaces.

```bash
make
make test
```

Expected result:

```text
LWP_PROTOCOL_TEST=PASS header_size=11
```

A safe integration test runs entirely inside two Linux network namespaces connected by a veth pair. It does not modify the host's physical NIC or kernel source.

```bash
sudo -v
./scripts/run_netns_test.sh
```

Expected final result:

```text
LWP_NETNS_TEST=PASS
```

## Short-Message Evaluation

The primary evaluation compares LWP, Linux TCP, and UDP on the same isolated namespace link using 2,000 messages per run, three repetitions, and payload sizes from 8 to 512 bytes.

The packet meter counts **all IPv4 bytes in both directions** for the benchmark flow. Therefore the LWP and TCP numbers include reverse ACK/control traffic rather than comparing data headers alone.

Median results from the validated run were:

| Payload | LWP overhead / useful | TCP overhead / useful | UDP overhead / useful | LWP total IPv4 reduction vs TCP |
|---:|---:|---:|---:|---:|
| 8 B | 4.363 | 12.543 | 3.500 | 60.4% |
| 16 B | 2.182 | 6.304 | 1.750 | 56.4% |
| 32 B | 1.091 | 3.141 | 0.875 | 49.5% |
| 64 B | 0.545 | 1.592 | 0.438 | 40.4% |
| 128 B | 0.273 | 0.791 | 0.219 | 28.9% |
| 256 B | 0.136 | 0.395 | 0.109 | 18.6% |
| 512 B | 0.068 | 0.191 | 0.055 | 10.3% |

The result matches the intended scope: the benefit is largest for short messages and decreases as payload size grows. UDP remains smaller because it does not provide LWP's reliable-delivery mechanism.

LWP is **not** claimed to be faster than TCP. In the measured userspace prototype, goodput did not consistently exceed Linux TCP. The main demonstrated benefit is reduced protocol traffic for short reliable messages.

Reproduce the benchmark with:

```bash
sudo -v
./scripts/run_short_message_benchmark.sh 2000 3
```

The resulting CSV is stored at `results/short_message_benchmark.csv`.

## Loss Robustness

ACK aggregation was also tested under controlled forward-path loss using `tc netem`. The workload was 1,000 messages of 64 bytes, sender window 8, RTO 100 ms, and three repetitions per loss rate.

| Forward loss | Delivery rate | Median retransmissions | Median timeouts |
|---:|---:|---:|---:|
| 0% | 1.000 | 0 | 0 |
| 0.1% | 1.000 | 0 | 0 |
| 1% | 1.000 | 72 | 9 |
| 5% | 1.000 | 424 | 53 |

All 12 validation runs delivered all 1,000 application messages without reported corruption. The 0.1% case is stochastic: only one of the three runs observed a loss event.

The loss experiment also exposes a limitation of the current prototype. With fixed 100 ms RTO and Go-Back-N recovery, goodput degrades sharply as loss increases. The project therefore treats loss recovery as a correctness property rather than claiming competitive lossy-network performance.

Reproduce the sweep with:

```bash
sudo -v
./scripts/run_loss_sweep.sh
```

Results are stored in `results/loss_sweep/`.

## Repository Structure

```text
LightWeightProtocol/
├── benchmarks/     # TCP/UDP baselines and packet meter
├── config/         # pre-registered LWP connection context
├── docs/           # protocol, evaluation, and historical notes
├── include/        # LWP headers
├── results/        # validated CSV/summary results
├── scripts/        # namespace setup and reproducible experiments
├── src/            # LWP sender/receiver/protocol implementation
└── tests/          # protocol serialization/corruption tests
```

## Scope and Limitations

LWP is a research/educational prototype, not a production TCP replacement.

- IPv4 only;
- Linux userspace raw sockets;
- experimental IPv4 protocol number 253 in an isolated test environment;
- pre-registered connection contexts rather than dynamic connection establishment;
- 16-bit sequence space with experiments kept below the wrap boundary;
- fixed sender window and fixed RTO;
- Go-Back-N recovery rather than selective retransmission;
- no production security, congestion-control, fairness, or Internet-deployment claim.

## Historical Context

The original Spring 2024 URP explored a lightweight reliable protocol with raw sockets, compact endpoint identifiers, acknowledgments, retransmission, and simple window control. The current repository preserves that problem statement while rebuilding the implementation with explicit wire serialization, a single Connection ID, reproducible tests, and new measurements.

See [`docs/historical-context.md`](docs/historical-context.md).

## References

- RFC 9293, *Transmission Control Protocol (TCP)*: https://www.rfc-editor.org/rfc/rfc9293.html
- RFC 768, *User Datagram Protocol*: https://www.rfc-editor.org/rfc/rfc768.html
- RFC 4727, *Experimental Values In IPv4, IPv6, ICMPv4, ICMPv6, UDP, and TCP Headers*: https://www.rfc-editor.org/rfc/rfc4727.html
- Linux `raw(7)`: https://man7.org/linux/man-pages/man7/raw.7.html
- Linux `tc-netem(8)`: https://man7.org/linux/man-pages/man8/tc-netem.8.html
