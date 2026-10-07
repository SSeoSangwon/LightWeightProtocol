#include "lwp_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    struct lwp_header header;
    struct lwp_header decoded;
    uint8_t payload[64];
    uint8_t packet[LWP_MAX_PACKET];
    size_t packet_length = 0;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_payload_length = 0;
    size_t i;

    memset(&header, 0, sizeof(header));
    header.version = LWP_VERSION;
    header.flags = LWP_FLAG_DATA | LWP_FLAG_ACK_REQ;
    header.connection_id = 100;
    header.sequence = 1234;
    header.acknowledgment = 1200;
    header.advertised_window = 8;

    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }

    require(lwp_build_packet(&header,
                             payload,
                             sizeof(payload),
                             packet,
                             sizeof(packet),
                             &packet_length) == 0,
            "packet build");
    require(packet_length == LWP_HEADER_SIZE + sizeof(payload),
            "packet length");
    require(lwp_parse_packet(packet,
                             packet_length,
                             &decoded,
                             &decoded_payload,
                             &decoded_payload_length) == 0,
            "packet parse");
    require(decoded.version == LWP_VERSION, "version");
    require(decoded.flags == (LWP_FLAG_DATA | LWP_FLAG_ACK_REQ), "flags");
    require(decoded.connection_id == 100, "connection id");
    require(decoded.sequence == 1234, "sequence");
    require(decoded.acknowledgment == 1200, "acknowledgment");
    require(decoded.advertised_window == 8, "advertised window");
    require(decoded_payload_length == sizeof(payload), "payload length");
    require(memcmp(decoded_payload, payload, sizeof(payload)) == 0,
            "payload equality");

    packet[LWP_HEADER_SIZE + 5] ^= 0x80u;
    require(lwp_parse_packet(packet,
                             packet_length,
                             &decoded,
                             &decoded_payload,
                             &decoded_payload_length) == -3,
            "checksum detects corruption");

    printf("LWP_PROTOCOL_TEST=PASS header_size=%u\n", LWP_HEADER_SIZE);
    return EXIT_SUCCESS;
}
