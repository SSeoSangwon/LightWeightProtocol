#include "lwp_protocol.h"

#include <arpa/inet.h>
#include <string.h>

static void write_u16(uint8_t *dst, uint16_t value)
{
    uint16_t network = htons(value);
    memcpy(dst, &network, sizeof(network));
}

static uint16_t read_u16(const uint8_t *src)
{
    uint16_t network;
    memcpy(&network, src, sizeof(network));
    return ntohs(network);
}

uint16_t lwp_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    size_t i = 0;

    while (i + 1 < length) {
        sum += ((uint16_t)data[i] << 8) | data[i + 1];
        i += 2;
    }

    if (i < length) {
        sum += (uint16_t)data[i] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

int lwp_encode_header(const struct lwp_header *header,
                      uint8_t out[LWP_HEADER_SIZE])
{
    if (header == NULL || out == NULL) {
        return -1;
    }

    out[0] = header->version;
    out[1] = header->flags;
    write_u16(out + 2, header->connection_id);
    write_u16(out + 4, header->sequence);
    write_u16(out + 6, header->acknowledgment);
    out[8] = header->advertised_window;
    write_u16(out + 9, header->checksum);
    return 0;
}

int lwp_decode_header(const uint8_t in[LWP_HEADER_SIZE],
                      struct lwp_header *header)
{
    if (in == NULL || header == NULL) {
        return -1;
    }

    header->version = in[0];
    header->flags = in[1];
    header->connection_id = read_u16(in + 2);
    header->sequence = read_u16(in + 4);
    header->acknowledgment = read_u16(in + 6);
    header->advertised_window = in[8];
    header->checksum = read_u16(in + 9);
    return 0;
}

int lwp_build_packet(const struct lwp_header *header,
                     const uint8_t *payload,
                     size_t payload_length,
                     uint8_t *out,
                     size_t out_capacity,
                     size_t *packet_length)
{
    struct lwp_header wire_header;
    size_t total_length;
    uint16_t checksum;

    if (header == NULL || out == NULL || packet_length == NULL) {
        return -1;
    }
    if (payload_length > LWP_MAX_PAYLOAD) {
        return -1;
    }
    if (payload_length > 0 && payload == NULL) {
        return -1;
    }

    total_length = LWP_HEADER_SIZE + payload_length;
    if (out_capacity < total_length) {
        return -1;
    }

    wire_header = *header;
    wire_header.checksum = 0;
    if (lwp_encode_header(&wire_header, out) != 0) {
        return -1;
    }

    if (payload_length > 0) {
        memcpy(out + LWP_HEADER_SIZE, payload, payload_length);
    }

    checksum = lwp_checksum(out, total_length);
    wire_header.checksum = checksum;
    if (lwp_encode_header(&wire_header, out) != 0) {
        return -1;
    }

    *packet_length = total_length;
    return 0;
}

int lwp_parse_packet(const uint8_t *packet,
                     size_t packet_length,
                     struct lwp_header *header,
                     const uint8_t **payload,
                     size_t *payload_length)
{
    uint8_t copy[LWP_MAX_PACKET];
    struct lwp_header decoded;
    uint16_t received_checksum;
    uint16_t calculated_checksum;

    if (packet == NULL || header == NULL || payload == NULL || payload_length == NULL) {
        return -1;
    }
    if (packet_length < LWP_HEADER_SIZE || packet_length > LWP_MAX_PACKET) {
        return -1;
    }

    if (lwp_decode_header(packet, &decoded) != 0) {
        return -1;
    }
    if (decoded.version != LWP_VERSION) {
        return -2;
    }

    memcpy(copy, packet, packet_length);
    received_checksum = decoded.checksum;
    copy[9] = 0;
    copy[10] = 0;
    calculated_checksum = lwp_checksum(copy, packet_length);
    if (received_checksum != calculated_checksum) {
        return -3;
    }

    *header = decoded;
    *payload = packet + LWP_HEADER_SIZE;
    *payload_length = packet_length - LWP_HEADER_SIZE;
    return 0;
}
