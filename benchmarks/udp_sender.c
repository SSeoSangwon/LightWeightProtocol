#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long long monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static void fill_payload(uint8_t *buf, size_t len, uint32_t seq)
{
    size_t i;
    buf[0] = (uint8_t)(seq >> 24);
    buf[1] = (uint8_t)(seq >> 16);
    buf[2] = (uint8_t)(seq >> 8);
    buf[3] = (uint8_t)seq;
    for (i = 4; i < len; ++i) {
        buf[i] = (uint8_t)((seq + i) & 0xffu);
    }
}

int main(int argc, char **argv)
{
    struct in_addr local_ip, peer_ip;
    struct sockaddr_in local_addr, peer_addr;
    unsigned long port, messages, payload_bytes, i;
    uint8_t *payload;
    int fd;
    long long start_us, end_us;

    if (argc != 6) {
        fprintf(stderr, "Usage: %s <local_ip> <peer_ip> <port> <messages> <payload_bytes>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (inet_pton(AF_INET, argv[1], &local_ip) != 1 || inet_pton(AF_INET, argv[2], &peer_ip) != 1) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return EXIT_FAILURE;
    }
    port = strtoul(argv[3], NULL, 10);
    messages = strtoul(argv[4], NULL, 10);
    payload_bytes = strtoul(argv[5], NULL, 10);
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

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = local_ip;
    local_addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        perror("bind");
        close(fd);
        free(payload);
        return EXIT_FAILURE;
    }
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr = peer_ip;
    peer_addr.sin_port = htons((uint16_t)port);

    start_us = monotonic_us();
    for (i = 1; i <= messages; ++i) {
        ssize_t n;
        fill_payload(payload, payload_bytes, (uint32_t)i);
        do {
            n = sendto(fd, payload, payload_bytes, 0,
                       (struct sockaddr *)&peer_addr, sizeof(peer_addr));
        } while (n < 0 && errno == EINTR);
        if (n != (ssize_t)payload_bytes) {
            perror("sendto");
            close(fd);
            free(payload);
            return EXIT_FAILURE;
        }
    }
    end_us = monotonic_us();

    printf("UDP_RESULT messages=%lu payload_bytes=%lu useful_bytes=%lu elapsed_us=%lld goodput_mbps=%.6f\n",
           messages, payload_bytes, messages * payload_bytes, end_us - start_us,
           (end_us > start_us)
               ? ((double)(messages * payload_bytes) * 8.0 / (double)(end_us - start_us))
               : 0.0);

    close(fd);
    free(payload);
    return EXIT_SUCCESS;
}
