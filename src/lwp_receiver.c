#define _POSIX_C_SOURCE 200809L

#include "lwp_context.h"
#include "lwp_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_ADVERTISED_WINDOW 8u

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s <contexts.conf> <connection_id> <local_ip> [advertised_window]\n",
            program);
}

static int payload_is_valid(const uint8_t *payload,
                            size_t length,
                            uint16_t sequence)
{
    size_t i;
    if (length == 0) {
        return 0;
    }
    if (length >= 2 &&
        (payload[0] != (uint8_t)(sequence >> 8) ||
         payload[1] != (uint8_t)(sequence & 0xFFu))) {
        return 0;
    }
    for (i = 2; i < length; i++) {
        if (payload[i] != (uint8_t)((sequence + i) & 0xFFu)) {
            return 0;
        }
    }
    return 1;
}

static int send_ack(int sock,
                    const struct sockaddr_in *peer_addr,
                    uint16_t connection_id,
                    uint16_t acknowledgment,
                    uint8_t advertised_window,
                    int fin)
{
    struct lwp_header header;
    uint8_t packet[LWP_HEADER_SIZE];
    size_t packet_length;
    ssize_t sent;

    memset(&header, 0, sizeof(header));
    header.version = LWP_VERSION;
    header.flags = LWP_FLAG_ACK | (fin ? LWP_FLAG_FIN : 0);
    header.connection_id = connection_id;
    header.sequence = 0;
    header.acknowledgment = acknowledgment;
    header.advertised_window = advertised_window;

    if (lwp_build_packet(&header, NULL, 0,
                         packet, sizeof(packet), &packet_length) != 0) {
        return -1;
    }

    sent = sendto(sock,
                  packet,
                  packet_length,
                  0,
                  (const struct sockaddr *)peer_addr,
                  sizeof(*peer_addr));
    return sent == (ssize_t)packet_length ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *config_path;
    unsigned long connection_arg;
    unsigned long window_arg = DEFAULT_ADVERTISED_WINDOW;
    struct lwp_context context;
    struct in_addr local_ip;
    struct in_addr peer_ip;
    char peer_text[INET_ADDRSTRLEN];
    int sock;
    struct sockaddr_in local_addr;
    struct sockaddr_in peer_addr;
    uint16_t expected_sequence = 1;
    unsigned long delivered_messages = 0;
    unsigned long delivered_bytes = 0;
    unsigned long duplicate_or_out_of_order = 0;
    unsigned long corrupted_packets = 0;
    unsigned long acknowledgments_sent = 0;
    uint8_t advertised_window;
    struct timespec start_ts;

    if (argc < 4 || argc > 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    config_path = argv[1];
    connection_arg = strtoul(argv[2], NULL, 10);
    if (argc == 5) {
        window_arg = strtoul(argv[4], NULL, 10);
    }

    if (connection_arg > 65535u || window_arg == 0 || window_arg > 255u) {
        fprintf(stderr, "Invalid numeric argument.\n");
        return EXIT_FAILURE;
    }
    advertised_window = (uint8_t)window_arg;

    if (inet_pton(AF_INET, argv[3], &local_ip) != 1) {
        fprintf(stderr, "Invalid local IPv4 address: %s\n", argv[3]);
        return EXIT_FAILURE;
    }

    if (lwp_load_context(config_path, (uint16_t)connection_arg, &context) != 0 ||
        lwp_resolve_peer(&context, &local_ip, &peer_ip) != 0) {
        fprintf(stderr, "Connection %lu does not match local IP %s in %s.\n",
                connection_arg, argv[3], config_path);
        return EXIT_FAILURE;
    }

    inet_ntop(AF_INET, &peer_ip, peer_text, sizeof(peer_text));

    sock = socket(AF_INET, SOCK_RAW, LWP_IP_PROTOCOL);
    if (sock < 0) {
        perror("socket(AF_INET, SOCK_RAW, 253)");
        fprintf(stderr, "Raw sockets require root or CAP_NET_RAW.\n");
        return EXIT_FAILURE;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = local_ip;
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr = peer_ip;

    printf("LWP receiver: connection=%u local=%s peer=%s advertised_window=%u header=%uB\n",
           context.connection_id,
           argv[3],
           peer_text,
           advertised_window,
           LWP_HEADER_SIZE);
    fflush(stdout);
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    for (;;) {
        uint8_t buffer[65535];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t received;
        struct iphdr *ip;
        size_t ip_header_length;
        size_t lwp_length;
        const uint8_t *lwp_packet;
        struct lwp_header header;
        const uint8_t *payload;
        size_t payload_length;
        int parse_result;

        received = recvfrom(sock,
                            buffer,
                            sizeof(buffer),
                            0,
                            (struct sockaddr *)&from,
                            &from_len);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            close(sock);
            return EXIT_FAILURE;
        }
        if ((size_t)received < sizeof(struct iphdr)) {
            continue;
        }

        ip = (struct iphdr *)buffer;
        ip_header_length = (size_t)ip->ihl * 4u;
        if (ip->version != 4 || ip->protocol != LWP_IP_PROTOCOL ||
            ip_header_length < sizeof(struct iphdr) ||
            (size_t)received < ip_header_length + LWP_HEADER_SIZE ||
            ip->saddr != peer_ip.s_addr) {
            continue;
        }

        lwp_length = (size_t)received - ip_header_length;
        lwp_packet = buffer + ip_header_length;
        parse_result = lwp_parse_packet(lwp_packet,
                                        lwp_length,
                                        &header,
                                        &payload,
                                        &payload_length);
        if (parse_result != 0) {
            corrupted_packets++;
            continue;
        }
        if (header.connection_id != context.connection_id) {
            continue;
        }

        if (header.flags & LWP_FLAG_FIN) {
            uint16_t last_delivered = expected_sequence == 1 ? 0 : (uint16_t)(expected_sequence - 1u);
            uint16_t fin_ack = header.sequence;
            if (send_ack(sock,
                         &peer_addr,
                         context.connection_id,
                         fin_ack,
                         advertised_window,
                         1) != 0) {
                perror("send FIN-ACK");
                close(sock);
                return EXIT_FAILURE;
            }
            acknowledgments_sent++;

            {
                struct timespec end_ts;
                long long elapsed_ms;
                clock_gettime(CLOCK_MONOTONIC, &end_ts);
                elapsed_ms = (long long)(end_ts.tv_sec - start_ts.tv_sec) * 1000LL +
                             (end_ts.tv_nsec - start_ts.tv_nsec) / 1000000LL;
                printf("LWP_RECEIVER_RESULT delivered_messages=%lu delivered_bytes=%lu last_delivered_seq=%u duplicate_or_out_of_order=%lu corrupted=%lu acknowledgments_sent=%lu elapsed_ms=%lld\n",
                       delivered_messages,
                       delivered_bytes,
                       last_delivered,
                       duplicate_or_out_of_order,
                       corrupted_packets,
                       acknowledgments_sent,
                       elapsed_ms);
                fflush(stdout);
            }
            break;
        }

        if ((header.flags & LWP_FLAG_DATA) == 0) {
            continue;
        }

        {
            int in_order = 0;
            int send_cumulative_ack = 0;

            if (header.sequence == expected_sequence &&
                payload_is_valid(payload, payload_length, header.sequence)) {
                delivered_messages++;
                delivered_bytes += payload_length;
                expected_sequence = (uint16_t)(expected_sequence + 1u);
                in_order = 1;
            } else {
                duplicate_or_out_of_order++;
            }

            /*
             * Normal in-order traffic is cumulatively acknowledged only when
             * the sender marks the end of its current transmission window.
             * Any duplicate/out-of-order packet triggers an immediate ACK so
             * the sender learns the current cumulative receive point.
             */
            if ((in_order && (header.flags & LWP_FLAG_ACK_REQ) != 0) || !in_order) {
                send_cumulative_ack = 1;
            }

            if (send_cumulative_ack) {
                uint16_t cumulative_ack = expected_sequence == 1 ? 0 : (uint16_t)(expected_sequence - 1u);
                if (send_ack(sock,
                             &peer_addr,
                             context.connection_id,
                             cumulative_ack,
                             advertised_window,
                             0) != 0) {
                    perror("send ACK");
                    close(sock);
                    return EXIT_FAILURE;
                }
                acknowledgments_sent++;
            }
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
