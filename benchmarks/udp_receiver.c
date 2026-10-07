#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static long long monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static int payload_valid(const uint8_t *buf, size_t len, uint32_t seq)
{
    size_t i;
    if (len < 4 || buf[0] != (uint8_t)(seq >> 24) ||
        buf[1] != (uint8_t)(seq >> 16) ||
        buf[2] != (uint8_t)(seq >> 8) || buf[3] != (uint8_t)seq) {
        return 0;
    }
    for (i = 4; i < len; ++i) {
        if (buf[i] != (uint8_t)((seq + i) & 0xffu)) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct in_addr local_ip;
    struct sockaddr_in addr;
    struct timeval timeout;
    unsigned long port, messages, payload_bytes;
    unsigned long delivered = 0, invalid = 0, out_of_order = 0;
    uint32_t expected = 1;
    uint8_t *payload;
    int fd;
    long long start_us = 0, end_us = 0;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <local_ip> <port> <messages> <payload_bytes>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (inet_pton(AF_INET, argv[1], &local_ip) != 1) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return EXIT_FAILURE;
    }
    port = strtoul(argv[2], NULL, 10);
    messages = strtoul(argv[3], NULL, 10);
    payload_bytes = strtoul(argv[4], NULL, 10);
    if (port == 0 || port > 65535 || messages == 0 || payload_bytes < 4 || payload_bytes > 65507) {
        fprintf(stderr, "Invalid numeric argument.\n");
        return EXIT_FAILURE;
    }

    payload = malloc(payload_bytes);
    if (payload == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        free(payload);
        return EXIT_FAILURE;
    }
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = local_ip;
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        free(payload);
        return EXIT_FAILURE;
    }

    while (delivered < messages) {
        ssize_t n = recvfrom(fd, payload, payload_bytes, 0, NULL, NULL);
        uint32_t seq;
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("recvfrom");
            close(fd);
            free(payload);
            return EXIT_FAILURE;
        }
        if (start_us == 0) {
            start_us = monotonic_us();
        }
        if ((size_t)n != payload_bytes) {
            invalid++;
            continue;
        }
        seq = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
              ((uint32_t)payload[2] << 8) | (uint32_t)payload[3];
        if (seq != expected) {
            out_of_order++;
            expected = seq + 1u;
        } else {
            expected++;
        }
        if (!payload_valid(payload, payload_bytes, seq)) {
            invalid++;
        }
        delivered++;
    }
    end_us = monotonic_us();

    printf("UDP_RECEIVER_RESULT delivered_messages=%lu delivered_bytes=%lu invalid=%lu out_of_order=%lu elapsed_us=%lld\n",
           delivered, delivered * payload_bytes, invalid, out_of_order,
           start_us == 0 ? 0 : end_us - start_us);

    close(fd);
    free(payload);
    return (delivered == messages && invalid == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
