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

static int recv_exact(int fd, uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, buf + off, len - off, 0);
        if (n == 0) {
            return 1;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct in_addr local_ip;
    struct sockaddr_in addr;
    unsigned long port, messages, payload_bytes, i;
    uint8_t *payload;
    int listener, conn, one = 1;
    unsigned long invalid = 0;
    long long start_us, end_us;

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
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        free(payload);
        return EXIT_FAILURE;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = local_ip;
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        perror("bind/listen");
        close(listener);
        free(payload);
        return EXIT_FAILURE;
    }
    conn = accept(listener, NULL, NULL);
    if (conn < 0) {
        perror("accept");
        close(listener);
        free(payload);
        return EXIT_FAILURE;
    }

    start_us = monotonic_us();
    for (i = 1; i <= messages; ++i) {
        int rc = recv_exact(conn, payload, payload_bytes);
        if (rc != 0) {
            fprintf(stderr, "TCP receiver ended early at message %lu.\n", i);
            close(conn);
            close(listener);
            free(payload);
            return EXIT_FAILURE;
        }
        if (!payload_valid(payload, payload_bytes, (uint32_t)i)) {
            invalid++;
        }
    }
    end_us = monotonic_us();

    printf("TCP_RECEIVER_RESULT delivered_messages=%lu delivered_bytes=%lu invalid=%lu elapsed_us=%lld\n",
           messages, messages * payload_bytes, invalid, end_us - start_us);

    close(conn);
    close(listener);
    free(payload);
    return invalid == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
