# LWP Protocol

## 1. Design Premise

LWP targets controlled networks in which the communication context is configured before data transfer. IPv4 continues to provide host addressing and routing. LWP replaces the transport header for an experiment-specific IPv4 protocol and uses a 16-bit Connection ID to identify the registered transport context.

The purpose is not to remove information that changes during reliable delivery. Sequence state, acknowledgment state, flow-window information, flags, and integrity information remain on the wire.

## 2. Wire Header

The LWP header is exactly 11 bytes and is serialized explicitly rather than transmitting a compiler-dependent C structure.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Version |
| 1 | 1 | Flags |
| 2 | 2 | Connection ID |
| 4 | 2 | Sequence |
| 6 | 2 | Acknowledgment |
| 8 | 1 | Advertised window |
| 9 | 2 | Checksum |

Payload length is derived from the IPv4 total length and IPv4 IHL.

Multi-byte fields use network byte order.

## 3. Flags

The implementation defines four control bits:

- `DATA`: packet carries application data;
- `ACK`: acknowledgment packet;
- `FIN`: transport termination;
- `ACK_REQ`: requests a cumulative ACK for the current transmission window.

## 4. Pre-Registered Connection Context

`config/contexts.conf` maps a Connection ID to two IPv4 endpoints. The sender and receiver validate packets against this context.

There is no SYN/SYN-ACK handshake in the current design. This is intentional: the experiment tests whether stable connection-identification metadata can be moved out of the per-packet data path in a pre-configured environment.

## 5. Reliable Delivery

The sender uses a fixed sliding window and cumulative acknowledgments. The receiver delivers only the next expected sequence number.

### ACK aggregation

Normal in-order DATA packets are not acknowledged individually. The sender marks the last DATA packet of each current transmission window, and the final DATA packet, with `ACK_REQ`. The receiver returns one cumulative ACK when such a packet is accepted in order.

A duplicate or out-of-order DATA packet triggers an immediate cumulative ACK containing the latest contiguous receive state.

### Timeout recovery

On retransmission timeout, the sender retransmits the outstanding window. This is Go-Back-N behavior.

The current implementation intentionally uses a fixed RTO to keep the transport mechanism small and reproducible. It is not presented as a competitive congestion/loss-recovery algorithm.

## 6. Integrity

The header and payload are protected by the LWP checksum. The protocol unit test verifies serialization/deserialization and checks that modified packet bytes fail validation.

## 7. Termination

After all DATA packets have been cumulatively acknowledged, the sender transmits FIN. The receiver responds with `ACK|FIN` and exits after reporting its delivery statistics.

## 8. Current Scope

- IPv4 only;
- experimental IPv4 protocol number 253;
- Linux userspace raw sockets;
- pre-registered connection context;
- 16-bit sequence number;
- experiments limited below sequence-number wrap;
- fixed window and fixed RTO;
- no Linux-kernel modification;
- no production security or Internet-deployment claim.
