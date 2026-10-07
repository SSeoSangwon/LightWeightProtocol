#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static long long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int port_matches(const uint8_t *transport, size_t transport_len, int protocol, uint16_t port)
{
    uint16_t src_port;
    uint16_t dst_port;

    if (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
        return 1;
    }
    if (transport_len < 4u) {
        return 0;
    }

    src_port = (uint16_t)(((uint16_t)transport[0] << 8) | transport[1]);
    dst_port = (uint16_t)(((uint16_t)transport[2] << 8) | transport[3]);
    return src_port == port || dst_port == port;
}

int main(int argc, char **argv)
{
    const char *ifname, *mode;
    struct in_addr ip_a, ip_b;
    unsigned long port_arg = 0;
    unsigned long idle_ms_arg = 250;
    int protocol;
    int fd;
    unsigned int ifindex;
    struct sockaddr_ll bind_addr;
    struct pollfd pfd;
    struct sigaction sa;
    unsigned long long packets = 0, ipv4_bytes = 0, l2_bytes = 0;
    unsigned long long a2b_packets = 0, a2b_ipv4_bytes = 0, a2b_l2_bytes = 0;
    unsigned long long b2a_packets = 0, b2a_ipv4_bytes = 0, b2a_l2_bytes = 0;
    long long started_ms, first_match_ms = -1, last_match_ms = -1;

    if (argc < 5 || argc > 7) {
        fprintf(stderr, "Usage: %s <ifname> <lwp|tcp|udp> <ip_a> <ip_b> [port] [idle_ms]\n", argv[0]);
        return EXIT_FAILURE;
    }
    ifname = argv[1];
    mode = argv[2];
    if (inet_pton(AF_INET, argv[3], &ip_a) != 1 || inet_pton(AF_INET, argv[4], &ip_b) != 1) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return EXIT_FAILURE;
    }
    if (strcmp(mode, "lwp") == 0) {
        protocol = 253;
    } else if (strcmp(mode, "tcp") == 0) {
        protocol = IPPROTO_TCP;
    } else if (strcmp(mode, "udp") == 0) {
        protocol = IPPROTO_UDP;
    } else {
        fprintf(stderr, "Invalid mode.\n");
        return EXIT_FAILURE;
    }
    if (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP) {
        if (argc < 6) {
            fprintf(stderr, "TCP/UDP mode requires a port.\n");
            return EXIT_FAILURE;
        }
        port_arg = strtoul(argv[5], NULL, 10);
        if (port_arg == 0 || port_arg > 65535) {
            fprintf(stderr, "Invalid port.\n");
            return EXIT_FAILURE;
        }
    }
    if (argc == 7) {
        idle_ms_arg = strtoul(argv[6], NULL, 10);
        if (idle_ms_arg < 50 || idle_ms_arg > 5000) {
            fprintf(stderr, "Invalid idle timeout.\n");
            return EXIT_FAILURE;
        }
    }

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        perror("if_nametoindex");
        return EXIT_FAILURE;
    }
    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    if (fd < 0) {
        perror("socket(AF_PACKET)");
        return EXIT_FAILURE;
    }
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sll_family = AF_PACKET;
    bind_addr.sll_protocol = htons(ETH_P_IP);
    bind_addr.sll_ifindex = (int)ifindex;
    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        perror("bind(AF_PACKET)");
        close(fd);
        return EXIT_FAILURE;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pfd.fd = fd;
    pfd.events = POLLIN;
    started_ms = monotonic_ms();

    while (!stop_requested) {
        uint8_t buf[65536];
        int prc = poll(&pfd, 1, 20);
        long long now = monotonic_ms();
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            close(fd);
            return EXIT_FAILURE;
        }
        if (prc > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            const struct ethhdr *eth;
            const struct iphdr *ip;
            size_t ip_offset = sizeof(struct ethhdr);
            size_t ihl, total_len, transport_len;
            const uint8_t *transport;
            int direction = 0;

            if (n < (ssize_t)(sizeof(struct ethhdr) + sizeof(struct iphdr))) {
                continue;
            }
            eth = (const struct ethhdr *)buf;
            if (ntohs(eth->h_proto) != ETH_P_IP) {
                continue;
            }
            ip = (const struct iphdr *)(buf + ip_offset);
            ihl = (size_t)ip->ihl * 4u;
            if (ip->version != 4 || ihl < sizeof(struct iphdr) ||
                (size_t)n < ip_offset + ihl || ip->protocol != protocol) {
                continue;
            }
            if (ip->saddr == ip_a.s_addr && ip->daddr == ip_b.s_addr) {
                direction = 1;
            } else if (ip->saddr == ip_b.s_addr && ip->daddr == ip_a.s_addr) {
                direction = 2;
            } else {
                continue;
            }
            total_len = ntohs(ip->tot_len);
            if (total_len < ihl || (size_t)n < ip_offset + total_len) {
                continue;
            }
            transport = (const uint8_t *)ip + ihl;
            transport_len = total_len - ihl;
            if (!port_matches(transport, transport_len, protocol, (uint16_t)port_arg)) {
                continue;
            }

            packets++;
            ipv4_bytes += total_len;
            l2_bytes += (unsigned long long)n;
            if (direction == 1) {
                a2b_packets++;
                a2b_ipv4_bytes += total_len;
                a2b_l2_bytes += (unsigned long long)n;
            } else {
                b2a_packets++;
                b2a_ipv4_bytes += total_len;
                b2a_l2_bytes += (unsigned long long)n;
            }
            if (first_match_ms < 0) {
                first_match_ms = now;
            }
            last_match_ms = now;
        }

        if (first_match_ms >= 0 && last_match_ms >= 0 &&
            now - last_match_ms >= (long long)idle_ms_arg) {
            break;
        }
        if (first_match_ms < 0 && now - started_ms >= 5000) {
            fprintf(stderr, "PACKET_METER_RESULT status=NO_TRAFFIC mode=%s\n", mode);
            close(fd);
            return EXIT_FAILURE;
        }
    }

    printf("PACKET_METER_RESULT status=PASS mode=%s packets=%llu ipv4_bytes=%llu l2_bytes=%llu a2b_packets=%llu a2b_ipv4_bytes=%llu a2b_l2_bytes=%llu b2a_packets=%llu b2a_ipv4_bytes=%llu b2a_l2_bytes=%llu\n",
           mode, packets, ipv4_bytes, l2_bytes,
           a2b_packets, a2b_ipv4_bytes, a2b_l2_bytes,
           b2a_packets, b2a_ipv4_bytes, b2a_l2_bytes);
    close(fd);
    return EXIT_SUCCESS;
}
