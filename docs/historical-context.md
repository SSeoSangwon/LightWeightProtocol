# Historical Context

The original Spring 2024 SKKU URP explored a lightweight reliable protocol using Linux raw sockets. The archived prototype used compact endpoint identifiers together with sequence/acknowledgment information, timeout-based retransmission, and a simple transmission-window idea.

The current public repository is a clean reimplementation built around the same problem statement: short application messages can spend a large fraction of their wire bytes on transport metadata, and a controlled environment can pre-register stable connection information instead of repeating all of it in every packet.

The reimplementation deliberately separates historical context from new validation. Historical benchmark numbers are not reused as measured results.

Major changes in the public implementation are:

1. `Source ID + Destination ID` are represented by one pre-registered Connection ID.
2. The wire format is serialized explicitly rather than sending a C structure directly.
3. Multi-byte fields use network byte order.
4. IPv4 receive parsing uses IHL instead of assuming a 20-byte IPv4 header.
5. Reliability is defined as cumulative-ACK sliding-window delivery with timeout-driven Go-Back-N recovery.
6. Window-level ACK aggregation reduces reverse-path control traffic.
7. Retransmission preserves the packet data associated with the sequence number.
8. Header/payload integrity is validated by checksum.
9. Correctness, short-message overhead, and controlled-loss behavior are evaluated with reproducible Linux namespace experiments.

This remains a Linux **userspace** transport prototype over raw IPv4 sockets. It does not modify the Linux kernel and is not presented as a production TCP replacement.
