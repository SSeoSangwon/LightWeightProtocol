#ifndef LWP_CONTEXT_H
#define LWP_CONTEXT_H

#include <netinet/in.h>
#include <stdint.h>

struct lwp_context {
    uint16_t connection_id;
    struct in_addr endpoint_a;
    struct in_addr endpoint_b;
};

int lwp_load_context(const char *path,
                     uint16_t connection_id,
                     struct lwp_context *context);

int lwp_resolve_peer(const struct lwp_context *context,
                     const struct in_addr *local,
                     struct in_addr *peer);

#endif
