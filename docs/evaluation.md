# Evaluation

## Short-Message Overhead

The primary experiment evaluates the original LWP motivation: reducing protocol traffic when application messages are short.

### Setup

- isolated Linux network namespaces connected by a veth pair;
- LWP, Linux TCP, and UDP compared on the same path;
- 2,000 messages per run;
- payload sizes: 8, 16, 32, 64, 128, 256, and 512 bytes;
- three repetitions per protocol/payload combination;
- TCP uses one persistent connection and `TCP_NODELAY`;
- UDP sends one datagram per application message;
- LWP uses the pre-registered reliable transport;
- packet meter counts IPv4 bytes for the benchmark flow in both directions.

The reported observed-overhead ratio is:

```text
(total observed IPv4 bytes - useful application payload bytes)
----------------------------------------------------------------
                  useful application payload bytes
```

This includes reverse ACK/control traffic. It is therefore different from simply comparing the minimum transport-header sizes.

### Median results

| Payload | LWP overhead/useful | TCP overhead/useful | UDP overhead/useful | LWP IPv4 reduction vs TCP |
|---:|---:|---:|---:|---:|
| 8 B | 4.363 | 12.543 | 3.500 | 60.4% |
| 16 B | 2.182 | 6.304 | 1.750 | 56.4% |
| 32 B | 1.091 | 3.141 | 0.875 | 49.5% |
| 64 B | 0.545 | 1.592 | 0.438 | 40.4% |
| 128 B | 0.273 | 0.791 | 0.219 | 28.9% |
| 256 B | 0.136 | 0.395 | 0.109 | 18.6% |
| 512 B | 0.068 | 0.191 | 0.055 | 10.3% |

The reduction is largest for the shortest messages and falls as payload size grows. This is the intended operating region of LWP.

UDP remains the smallest because it does not implement LWP's reliable-delivery behavior.

The userspace LWP implementation did not consistently outperform Linux TCP in goodput, so the evaluation does not make a throughput-superiority claim.

## ACK Aggregation

An earlier implementation returned one LWP ACK per DATA packet. For a 2,000-message benchmark this produced 62,031 reverse-path IPv4 bytes.

Window-level cumulative ACK aggregation reduced this to 7,781 bytes in the no-loss benchmark, an 87.456% reduction in LWP reverse ACK traffic, without changing the 11-byte header format.

## Loss Robustness

A separate validation checks reliable delivery after ACK aggregation.

### Setup

- 1,000 messages;
- 64-byte payload;
- sender window 8;
- fixed RTO 100 ms;
- forward DATA-path loss injected with `tc netem`;
- loss rates: 0%, 0.1%, 1%, and 5%;
- three repetitions per loss rate.

### Median results

| Forward loss | Delivery rate | Retransmissions | Timeouts | ACK requests | ACKs sent | Sender goodput |
|---:|---:|---:|---:|---:|---:|---:|
| 0% | 1.000 | 0 | 0 | 125 | 126 | 51.200 Mbps |
| 0.1% | 1.000 | 0 | 0 | 125 | 126 | 56.889 Mbps |
| 1% | 1.000 | 72 | 9 | 543 | 592 | 0.562 Mbps |
| 5% | 1.000 | 424 | 53 | 870 | 1236 | 0.096 Mbps |

Every one of the 12 runs delivered all 1,000 messages with no reported corruption.

The 0.1% median shows zero retransmissions because two of the three stochastic runs did not experience a loss event. The experiment is used as a correctness/robustness check rather than as a precise loss-performance model.

The sharp goodput drop at 1% and 5% is a known limitation of fixed-RTO Go-Back-N recovery.
