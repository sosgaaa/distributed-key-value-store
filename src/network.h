#ifndef DKVS_NETWORK_H
#define DKVS_NETWORK_H

#include "dkvs.h"

typedef struct {
    const ring_t *ring;
    size_t replicas;
    size_t read_quorum;
    size_t write_quorum;
    int timeout_ms;
} client_t;

/* Returns 0 on quorum success, -1 otherwise. Caller owns *value on success. */
int network_get(const client_t *client, const char *key, char **value);
int network_put(const client_t *client, const char *key, const char *value);

#endif
