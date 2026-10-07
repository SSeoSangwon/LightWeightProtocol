#include "lwp_context.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int lwp_load_context(const char *path,
                     uint16_t connection_id,
                     struct lwp_context *context)
{
    FILE *file;
    char line[512];

    if (path == NULL || context == NULL) {
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned int id;
        char a[INET_ADDRSTRLEN];
        char b[INET_ADDRSTRLEN];
        char *p = line;

        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (sscanf(p, "%u %15s %15s", &id, a, b) != 3) {
            fclose(file);
            return -2;
        }
        if (id > 65535u) {
            fclose(file);
            return -2;
        }
        if ((uint16_t)id != connection_id) {
            continue;
        }

        memset(context, 0, sizeof(*context));
        context->connection_id = (uint16_t)id;
        if (inet_pton(AF_INET, a, &context->endpoint_a) != 1 ||
            inet_pton(AF_INET, b, &context->endpoint_b) != 1) {
            fclose(file);
            return -2;
        }

        fclose(file);
        return 0;
    }

    fclose(file);
    return -3;
}

int lwp_resolve_peer(const struct lwp_context *context,
                     const struct in_addr *local,
                     struct in_addr *peer)
{
    if (context == NULL || local == NULL || peer == NULL) {
        return -1;
    }

    if (context->endpoint_a.s_addr == local->s_addr) {
        *peer = context->endpoint_b;
        return 0;
    }
    if (context->endpoint_b.s_addr == local->s_addr) {
        *peer = context->endpoint_a;
        return 0;
    }

    return -2;
}
