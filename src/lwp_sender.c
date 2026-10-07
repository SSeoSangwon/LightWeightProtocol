#define _POSIX_C_SOURCE 200809L

#include "lwp_context.h"
#include "lwp_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_WINDOW 8u
#define DEFAULT_RTO_MS 200u
#define MAX_WINDOW 64u
#define MAX_MESSAGES 60000u

struct outstanding_packet {
    uint16_t sequence;
    size_t packet_length;
    uint8_t packet[LWP_MAX_PACKET];
    int valid;
};

static long long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s <contexts.conf> <connection_id> <local_ip> "
            "<messages> <payload_bytes> [window] [rto_ms]\n",
            program);
}

static void fill_payload(uint8_t *payload, size_t length, uint16_t sequence)
{
    size_t i;
    if (length >= 2) {
        payload[0] = (uint8_t)(sequence >> 8);
        payload[1] = (uint8_t)(sequence & 0xFFu);
    }
    for (i = 2; i < length; i++) {
        payload[i] = (uint8_t)((sequence + i) & 0xFFu);
    }
}

static int send_wire_packet(int sock,
                            const struct sockaddr_in *peer_addr,
                            const uint8_t *packet,
                            size_t packet_length)
{
    ssize_t sent = sendto(sock,
                          packet,
                          packet_length,
                          0,
                          (const struct sockaddr *)peer_addr,
                          sizeof(*peer_addr));
    return sent == (ssize_t)packet_length ? 0 : -1;
}

static int receive_ack(int sock,
                       uint16_t connection_id,
                       const struct in_addr *expected_peer,
                       uint16_t *acknowledgment,
                       uint8_t *flags,
                       uint8_t *advertised_window)
{
    uint8_t buffer[65535];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t received;
    struct iphdr *ip;
    size_t ip_header_length;
    size_t ip_payload_length;
    const uint8_t *lwp_packet;
    struct lwp_header header;
    const uint8_t *payload;
    size_t payload_length;

    received = recvfrom(sock,
                        buffer,
                        sizeof(buffer),
                        0,
                        (struct sockaddr *)&from,
                        &from_len);
    if (received < 0) {
        return errno == EINTR ? 1 : -1;
    }
    if ((size_t)received < sizeof(struct iphdr)) {
        return 1;
    }

    ip = (struct iphdr *)buffer;
    ip_header_length = (size_t)ip->ihl * 4u;
    if (ip->version != 4 || ip->protocol != LWP_IP_PROTOCOL ||
        ip_header_length < sizeof(struct iphdr) ||
        (size_t)received < ip_header_length + LWP_HEADER_SIZE) {
        return 1;
    }
    if (ip->saddr != expected_peer->s_addr) {
        return 1;
    }

    ip_payload_length = (size_t)received - ip_header_length;
    lwp_packet = buffer + ip_header_length;
    if (lwp_parse_packet(lwp_packet,
                         ip_payload_length,
                         &header,
                         &payload,
                         &payload_length) != 0) {
        return 1;
    }
    (void)payload;
    (void)payload_length;

    if (header.connection_id != connection_id ||
        (header.flags & LWP_FLAG_ACK) == 0) {
        return 1;
    }

    *acknowledgment = header.acknowledgment;
    *flags = header.flags;
    *advertised_window = header.advertised_window;
    return 0;
}

int main(int argc, char **argv)
{
    const char *config_path;
    unsigned long connection_arg;
    unsigned long messages_arg;
    unsigned long payload_arg;
    unsigned long window_arg = DEFAULT_WINDOW;
    unsigned long rto_arg = DEFAULT_RTO_MS;
    struct lwp_context context;
    struct in_addr local_ip;
    struct in_addr peer_ip;
    char peer_text[INET_ADDRSTRLEN];
    int sock;
    struct sockaddr_in local_addr;
    struct sockaddr_in peer_addr;
    struct outstanding_packet slots[MAX_WINDOW];
    uint16_t base = 1;
    uint16_t next = 1;
    uint16_t total_messages;
    uint16_t fin_sequence;
    size_t payload_length;
    uint8_t window_size;
    uint8_t peer_window;
    unsigned int rto_ms;
    unsigned long original_transmissions = 0;
    unsigned long retransmissions = 0;
    unsigned long timeout_events = 0;
    unsigned long ack_requests = 0;
    long long timer_started = 0;
    long long start_ms;
    int done = 0;

    if (argc < 6 || argc > 8) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    config_path = argv[1];
    connection_arg = strtoul(argv[2], NULL, 10);
    messages_arg = strtoul(argv[4], NULL, 10);
    payload_arg = strtoul(argv[5], NULL, 10);
    if (argc >= 7) {
        window_arg = strtoul(argv[6], NULL, 10);
    }
    if (argc >= 8) {
        rto_arg = strtoul(argv[7], NULL, 10);
    }

    if (connection_arg > 65535u || messages_arg == 0 || messages_arg > MAX_MESSAGES ||
        payload_arg == 0 || payload_arg > LWP_MAX_PAYLOAD ||
        window_arg == 0 || window_arg > MAX_WINDOW || window_arg > 255u ||
        rto_arg == 0 || rto_arg > 60000u) {
        fprintf(stderr, "Invalid numeric argument.\n");
        return EXIT_FAILURE;
    }

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
    total_messages = (uint16_t)messages_arg;
    fin_sequence = (uint16_t)(total_messages + 1u);
    payload_length = (size_t)payload_arg;
    window_size = (uint8_t)window_arg;
    peer_window = window_size;
    rto_ms = (unsigned int)rto_arg;
    memset(slots, 0, sizeof(slots));

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

    printf("LWP sender: connection=%u local=%s peer=%s messages=%u payload=%zu window=%u rto=%ums header=%uB\n",
           context.connection_id,
           argv[3],
           peer_text,
           total_messages,
           payload_length,
           window_size,
           rto_ms,
           LWP_HEADER_SIZE);

    start_ms = monotonic_ms();

    while (!done) {
        uint8_t effective_window = peer_window < window_size ? peer_window : window_size;
        if (effective_window == 0) {
            effective_window = 1;
        }
        while (next <= total_messages && (uint16_t)(next - base) < effective_window) {
            struct lwp_header header;
            uint8_t payload[LWP_MAX_PAYLOAD];
            size_t slot_index = (size_t)(next % MAX_WINDOW);
            struct outstanding_packet *slot = &slots[slot_index];

            fill_payload(payload, payload_length, next);
            memset(&header, 0, sizeof(header));
            header.version = LWP_VERSION;
            header.flags = LWP_FLAG_DATA;
            if ((uint16_t)(next - base + 1u) == effective_window || next == total_messages) {
                header.flags |= LWP_FLAG_ACK_REQ;
                ack_requests++;
            }
            header.connection_id = context.connection_id;
            header.sequence = next;
            header.acknowledgment = 0;
            header.advertised_window = window_size;

            if (lwp_build_packet(&header,
                                 payload,
                                 payload_length,
                                 slot->packet,
                                 sizeof(slot->packet),
                                 &slot->packet_length) != 0) {
                fprintf(stderr, "Failed to build packet %u.\n", next);
                close(sock);
                return EXIT_FAILURE;
            }
            slot->sequence = next;
            slot->valid = 1;

            if (send_wire_packet(sock, &peer_addr, slot->packet, slot->packet_length) != 0) {
                perror("sendto");
                close(sock);
                return EXIT_FAILURE;
            }
            original_transmissions++;
            if (base == next) {
                timer_started = monotonic_ms();
            }
            next++;
        }

        if (base > total_messages) {
            struct lwp_header fin_header;
            uint8_t fin_packet[LWP_HEADER_SIZE];
            size_t fin_length;
            long long fin_deadline;

            memset(&fin_header, 0, sizeof(fin_header));
            fin_header.version = LWP_VERSION;
            fin_header.flags = LWP_FLAG_FIN;
            fin_header.connection_id = context.connection_id;
            fin_header.sequence = fin_sequence;
            fin_header.advertised_window = window_size;
            if (lwp_build_packet(&fin_header, NULL, 0,
                                 fin_packet, sizeof(fin_packet), &fin_length) != 0) {
                close(sock);
                return EXIT_FAILURE;
            }

            for (;;) {
                struct pollfd pfd;
                uint16_t ack = 0;
                uint8_t flags = 0;
                int poll_result;

                if (send_wire_packet(sock, &peer_addr, fin_packet, fin_length) != 0) {
                    perror("sendto FIN");
                    close(sock);
                    return EXIT_FAILURE;
                }
                fin_deadline = monotonic_ms() + rto_ms;

                while (monotonic_ms() < fin_deadline) {
                    int remaining = (int)(fin_deadline - monotonic_ms());
                    pfd.fd = sock;
                    pfd.events = POLLIN;
                    pfd.revents = 0;
                    poll_result = poll(&pfd, 1, remaining > 0 ? remaining : 1);
                    if (poll_result > 0 && (pfd.revents & POLLIN)) {
                        int rc = receive_ack(sock,
                                             context.connection_id,
                                             &peer_ip,
                                             &ack,
                                             &flags,
                                             &peer_window);
                        if (rc == 0 && (flags & LWP_FLAG_FIN) && ack == fin_sequence) {
                            done = 1;
                            break;
                        }
                    }
                }
                if (done) {
                    break;
                }
                timeout_events++;
                retransmissions++;
            }
            continue;
        }

        {
            long long now = monotonic_ms();
            long long elapsed = now - timer_started;
            int wait_ms = elapsed >= (long long)rto_ms ? 0 : (int)(rto_ms - elapsed);
            struct pollfd pfd;
            int poll_result;

            pfd.fd = sock;
            pfd.events = POLLIN;
            pfd.revents = 0;
            poll_result = poll(&pfd, 1, wait_ms);

            if (poll_result > 0 && (pfd.revents & POLLIN)) {
                uint16_t ack = 0;
                uint8_t flags = 0;
                int rc = receive_ack(sock,
                                     context.connection_id,
                                     &peer_ip,
                                     &ack,
                                     &flags,
                                     &peer_window);
                if (rc == 0 && ack >= base && ack <= total_messages) {
                    uint16_t seq;
                    for (seq = base; seq <= ack; seq++) {
                        size_t slot_index = (size_t)(seq % MAX_WINDOW);
                        if (slots[slot_index].valid && slots[slot_index].sequence == seq) {
                            slots[slot_index].valid = 0;
                        }
                    }
                    base = (uint16_t)(ack + 1u);
                    timer_started = base < next ? monotonic_ms() : 0;
                }
            } else if (poll_result == 0 ||
                       (timer_started != 0 && monotonic_ms() - timer_started >= (long long)rto_ms)) {
                uint16_t seq;
                timeout_events++;
                for (seq = base; seq < next; seq++) {
                    size_t slot_index = (size_t)(seq % MAX_WINDOW);
                    struct outstanding_packet *slot = &slots[slot_index];
                    if (!slot->valid || slot->sequence != seq) {
                        continue;
                    }
                    if (send_wire_packet(sock, &peer_addr, slot->packet, slot->packet_length) != 0) {
                        perror("retransmit sendto");
                        close(sock);
                        return EXIT_FAILURE;
                    }
                    retransmissions++;
                }
                timer_started = monotonic_ms();
            } else if (poll_result < 0 && errno != EINTR) {
                perror("poll");
                close(sock);
                return EXIT_FAILURE;
            }
        }
    }

    {
        long long elapsed_ms = monotonic_ms() - start_ms;
        unsigned long useful_bytes = (unsigned long)total_messages * payload_length;
        double goodput_mbps = elapsed_ms > 0
            ? ((double)useful_bytes * 8.0) / ((double)elapsed_ms * 1000.0)
            : 0.0;
        printf("LWP_RESULT messages=%u payload_bytes=%zu useful_bytes=%lu original_tx=%lu ack_requests=%lu retransmissions=%lu timeouts=%lu elapsed_ms=%lld goodput_mbps=%.6f\n",
               total_messages,
               payload_length,
               useful_bytes,
               original_transmissions,
               ack_requests,
               retransmissions,
               timeout_events,
               elapsed_ms,
               goodput_mbps);
    }

    close(sock);
    return EXIT_SUCCESS;
}
