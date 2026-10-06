#include "dkvs.h"
#include "args.h"

#include <arpa/inet.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_nodes(const void *left, const void *right)
{
    const node_t *a = left;
    const node_t *b = right;
    int digest = memcmp(a->digest, b->digest, sizeof(a->digest));
    if (digest != 0) return digest;
    int ip = strcmp(a->ip, b->ip);
    if (ip != 0) return ip;
    if (a->port != b->port) return a->port < b->port ? -1 : 1;
    return a->virtual_id < b->virtual_id ? -1 : a->virtual_id > b->virtual_id;
}

static int same_server(const node_t *a, const node_t *b)
{
    return a->port == b->port && strcmp(a->ip, b->ip) == 0;
}

int ring_load(ring_t *ring, const char *path)
{
    if (ring == NULL || path == NULL) return -1;
    *ring = (ring_t) {0};
    FILE *file = fopen(path, "r");
    if (file == NULL) return -1;
    char *line = NULL;
    size_t line_size = 0;
    int status = 0;
    while (getline(&line, &line_size, file) >= 0) {
        char ip[INET_ADDRSTRLEN];
        char port_text[32], copies_text[32];
        uint64_t port, copies;
        char extra;
        char *comment = strchr(line, '#');
        if (comment != NULL) *comment = '\0';
        int fields = sscanf(line, " %15s %31s %31s %c", ip, port_text, copies_text, &extra);
        if (fields == -1) continue;
        if (fields != 3 || parse_unsigned(port_text, 65535, &port) != 0 || port == 0 ||
            parse_unsigned(copies_text, MAX_NODES, &copies) != 0 || copies == 0 ||
            ring->count + copies > MAX_NODES) {
            status = -1;
            break;
        }
        struct in_addr address;
        if (inet_pton(AF_INET, ip, &address) != 1) {
            status = -1;
            break;
        }
        for (size_t i = 0; i < ring->count; ++i) {
            if (ring->nodes[i].port == port && strcmp(ring->nodes[i].ip, ip) == 0) {
                status = -1;
                break;
            }
        }
        if (status != 0) break;
        node_t *resized = realloc(ring->nodes, (ring->count + copies) * sizeof(*ring->nodes));
        if (resized == NULL) {
            status = -1;
            break;
        }
        ring->nodes = resized;
        for (unsigned int id = 1; id <= copies; ++id) {
            node_t *node = &ring->nodes[ring->count++];
            memset(node, 0, sizeof(*node));
            snprintf(node->ip, sizeof(node->ip), "%s", ip);
            node->port = (unsigned short) port;
            node->virtual_id = id;
            node->address.sin_family = AF_INET;
            node->address.sin_port = htons(node->port);
            node->address.sin_addr = address;
            char name[96];
            int length = snprintf(name, sizeof(name), "%s %u %u", ip, (unsigned int) port, id);
            if (length < 0 || (size_t) length >= sizeof(name)) {
                status = -1;
                break;
            }
            SHA1((const unsigned char *) name, (size_t) length, node->digest);
        }
        if (status != 0) break;
        ++ring->server_count;
    }
    if (ferror(file) != 0) status = -1;
    free(line);
    fclose(file);
    if (status == 0 && ring->count > 0) {
        qsort(ring->nodes, ring->count, sizeof(*ring->nodes), compare_nodes);
        return 0;
    }
    ring_destroy(ring);
    return -1;
}

void ring_destroy(ring_t *ring)
{
    if (ring == NULL) return;
    free(ring->nodes);
    *ring = (ring_t) {0};
}

void ring_print(const ring_t *ring)
{
    for (size_t i = 0; i < ring->count; ++i) {
        const node_t *node = &ring->nodes[i];
        printf("%s:%u vnode=%u sha=", node->ip, node->port, node->virtual_id);
        for (size_t j = 0; j < sizeof(node->digest); ++j) printf("%02x", node->digest[j]);
        putchar('\n');
    }
}

size_t ring_select(const ring_t *ring, const char *key, size_t limit, node_t *selected)
{
    if (ring == NULL || key == NULL || selected == NULL || ring->count == 0) return 0;
    unsigned char digest[20];
    SHA1((const unsigned char *) key, strlen(key), digest);
    size_t first = 0;
    while (first < ring->count && memcmp(ring->nodes[first].digest, digest, 20) < 0) ++first;
    if (first == ring->count) first = 0;
    size_t count = 0;
    for (size_t offset = 0; offset < ring->count && count < limit; ++offset) {
        const node_t *candidate = &ring->nodes[(first + offset) % ring->count];
        int duplicate = 0;
        for (size_t j = 0; j < count; ++j) {
            if (same_server(candidate, &selected[j])) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) selected[count++] = *candidate;
    }
    return count;
}
