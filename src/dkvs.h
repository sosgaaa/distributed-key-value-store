#ifndef DKVS_H
#define DKVS_H

#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

enum { MAX_ELEMENT = 512, MAX_MESSAGE = 2 * MAX_ELEMENT + 1, MAX_NODES = 1024 };

typedef struct entry {
    char *key;
    char *value;
    struct entry *next;
} entry_t;

typedef struct {
    entry_t **buckets;
    size_t capacity;
    pthread_mutex_t mutex;
} store_t;

int store_init(store_t *store, size_t capacity);
void store_destroy(store_t *store);
int store_put(store_t *store, const char *key, const char *value);
/* Caller owns the returned string; NULL means the key is absent. */
char *store_get(store_t *store, const char *key);

typedef struct {
    struct sockaddr_in address;
    unsigned char digest[20];
    char ip[INET_ADDRSTRLEN];
    unsigned short port;
    unsigned int virtual_id;
} node_t;

typedef struct {
    node_t *nodes;
    size_t count;
    size_t server_count;
} ring_t;

int ring_load(ring_t *ring, const char *path);
void ring_destroy(ring_t *ring);
/* Returns the number of distinct physical servers, up to limit. */
size_t ring_select(const ring_t *ring, const char *key, size_t limit, node_t *selected);

#endif
