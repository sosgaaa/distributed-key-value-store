/* Distributed GET/PUT operations with distinct-server quorums. */
#include "network.h"
#include "socket_layer.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *value;
    size_t votes;
} vote_t;

static int valid_key(const char *key)
{
    return key != NULL && key[0] != 0 && strlen(key) <= MAX_ELEMENT;
}

static int exchange(const client_t *client, const char *key, const char *value, char **answer)
{
    int writing = value != NULL;
    if (client == NULL || client->ring == NULL || !valid_key(key) ||
        (writing && strlen(value) > MAX_ELEMENT) || client->replicas == 0 ||
        client->replicas > client->ring->server_count || client->timeout_ms <= 0 ||
        client->read_quorum == 0 || client->read_quorum > client->replicas ||
        client->write_quorum == 0 || client->write_quorum > client->replicas) return -1;

    size_t n = client->replicas;
    node_t *nodes = calloc(n, sizeof(*nodes));
    unsigned char *sent = calloc(n, 1);
    unsigned char *seen = calloc(n, 1);
    vote_t *votes = calloc(n, sizeof(*votes));
    int fd = -1, result = -1;
    size_t distinct = 0;
    if (nodes == NULL || sent == NULL || seen == NULL || votes == NULL ||
        ring_select(client->ring, key, n, nodes) != n) goto done;
    fd = udp_open();
    if (fd < 0) goto done;
    unsigned char request[MAX_MESSAGE];
    size_t size = strlen(key);
    memcpy(request, key, size);
    if (writing) {
        request[size++] = 0;
        size_t value_size = strlen(value);
        memcpy(request + size, value, value_size);
        size += value_size;
    }

    /* Send to ALL replicas before waiting, even when W=1. */
    int64_t deadline = udp_deadline(client->timeout_ms);
    if (deadline < 0) goto done;
    size_t outstanding = 0, acknowledgements = 0;
    for (size_t i = 0; i < n; ++i) {
        if (udp_send(fd, request, size, &nodes[i].address) == 0) {
            sent[i] = 1;
            ++outstanding;
        }
    }
    debug_log("%s: sent to %zu/%zu servers\n", writing ? "put" : "get", outstanding, n);
    while (outstanding > 0) {
        unsigned char response[MAX_MESSAGE + 1];
        struct sockaddr_in sender;
        ssize_t length = udp_receive_until(fd, response, sizeof(response), &sender, deadline);
        if (length < 0) break;
        size_t i = 0;
        while (i < n && !udp_same_peer(&nodes[i].address, &sender)) ++i;
        /* Unknown, uncontacted and duplicate peers can never cast a vote. */
        if (i == n || !sent[i] || seen[i]) continue;
        if (writing) {
            if (length != 0 && !(length == 1 && response[0] == 0)) continue;
            seen[i] = 1;
            --outstanding;
            /* The course protocol uses one NUL byte for a successful PUT. */
            if (length == 1 && ++acknowledgements >= client->write_quorum) {
                result = 0;
                break;
            }
        } else {
            if (length == 1 && response[0] == 0) {
                seen[i] = 1;
                --outstanding;
                continue;
            }
            if (length > MAX_ELEMENT || memchr(response, 0, (size_t) length) != NULL) continue;
            seen[i] = 1;
            --outstanding;
            response[length] = 0;
            size_t k = 0;
            while (k < distinct && strcmp(votes[k].value, (char *) response) != 0) ++k;
            if (k == distinct) {
                votes[k].value = strdup((char *) response);
                if (votes[k].value == NULL) goto done;
                ++distinct;
            }
            if (++votes[k].votes >= client->read_quorum) {
                *answer = votes[k].value;
                votes[k].value = NULL;
                result = 0;
                break;
            }
        }
    }
done:
    if (fd >= 0) close(fd);
    if (votes != NULL) for (size_t i = 0; i < distinct; ++i) free(votes[i].value);
    free(votes);
    free(seen);
    free(sent);
    free(nodes);
    return result;
}

int network_get(const client_t *client, const char *key, char **value)
{
    if (value == NULL) return -1;
    *value = NULL;
    return exchange(client, key, NULL, value);
}

int network_put(const client_t *client, const char *key, const char *value)
{
    if (value == NULL) return -1;
    return exchange(client, key, value, NULL);
}
