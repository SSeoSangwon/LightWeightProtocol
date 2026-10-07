#ifndef LWP_PROTOCOL_H
#define LWP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define LWP_VERSION 1u
#define LWP_IP_PROTOCOL 253
#define LWP_HEADER_SIZE 11u
#define LWP_MAX_PAYLOAD 1400u
#define LWP_MAX_PACKET (LWP_HEADER_SIZE + LWP_MAX_PAYLOAD)

#define LWP_FLAG_DATA 0x01u
#define LWP_FLAG_ACK  0x02u
#define LWP_FLAG_FIN  0x04u
#define LWP_FLAG_ACK_REQ 0x08u

struct lwp_header {
    uint8_t version;
    uint8_t flags;
    uint16_t connection_id;
    uint16_t sequence;
    uint16_t acknowledgment;
    uint8_t advertised_window;
    uint16_t checksum;
};

uint16_t lwp_checksum(const uint8_t *data, size_t length);

int lwp_encode_header(const struct lwp_header *header,
                      uint8_t out[LWP_HEADER_SIZE]);

int lwp_decode_header(const uint8_t in[LWP_HEADER_SIZE],
                      struct lwp_header *header);

int lwp_build_packet(const struct lwp_header *header,
                     const uint8_t *payload,
                     size_t payload_length,
                     uint8_t *out,
                     size_t out_capacity,
                     size_t *packet_length);

int lwp_parse_packet(const uint8_t *packet,
                     size_t packet_length,
                     struct lwp_header *header,
                     const uint8_t **payload,
                     size_t *payload_length);

#endif
