#include "dkvs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static size_t hash_key(const char *key, size_t capacity)
{
    /* Adapted from idbenj's MIT-licensed hash_function (see LICENSES/). */
    size_t hash = 0;
    for (size_t i = 0; key[i]; ++i) {
        hash += (unsigned char) key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash % capacity;
}

int store_init(store_t *store, size_t capacity)
{
    if (store == NULL || capacity == 0) return -1;
    store->buckets = calloc(capacity, sizeof(*store->buckets));
    if (store->buckets == NULL) return -1;
    store->capacity = capacity;
    if (pthread_mutex_init(&store->mutex, NULL) != 0) {
        free(store->buckets);
        store->buckets = NULL;
        return -1;
    }
    return 0;
}

void store_destroy(store_t *store)
{
    if (store == NULL || store->buckets == NULL) return;
    for (size_t i = 0; i < store->capacity; ++i) {
        entry_t *entry = store->buckets[i];
        while (entry != NULL) {
            entry_t *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(store->buckets);
    store->buckets = NULL;
    pthread_mutex_destroy(&store->mutex);
}

int store_put(store_t *store, const char *key, const char *value)
{
    if (store == NULL || store->buckets == NULL || key == NULL || value == NULL ||
        key[0] == 0 || strlen(key) > MAX_ELEMENT || strlen(value) > MAX_ELEMENT) return -1;
    char *copy = strdup(value);
    if (copy == NULL) return -1;
    pthread_mutex_lock(&store->mutex);
    size_t slot = hash_key(key, store->capacity);
    for (entry_t *entry = store->buckets[slot]; entry != NULL; entry = entry->next) {
        if (strcmp(entry->key, key) == 0) {
            free(entry->value);
            entry->value = copy;
            pthread_mutex_unlock(&store->mutex);
            return 0;
        }
    }
    entry_t *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        pthread_mutex_unlock(&store->mutex);
        free(copy);
        return -1;
    }
    entry->key = strdup(key);
    if (entry->key == NULL) {
        pthread_mutex_unlock(&store->mutex);
        free(entry);
        free(copy);
        return -1;
    }
    entry->value = copy;
    entry->next = store->buckets[slot];
    store->buckets[slot] = entry;
    pthread_mutex_unlock(&store->mutex);
    return 0;
}

char *store_get(store_t *store, const char *key)
{
    if (store == NULL || store->buckets == NULL || key == NULL) return NULL;
    pthread_mutex_lock(&store->mutex);
    size_t slot = hash_key(key, store->capacity);
    char *result = NULL;
    for (entry_t *entry = store->buckets[slot]; entry != NULL; entry = entry->next) {
        if (strcmp(entry->key, key) == 0) {
            result = strdup(entry->value);
            break;
        }
    }
    pthread_mutex_unlock(&store->mutex);
    return result;
}

char *store_snapshot(store_t *store, size_t *length)
{
    if (store == NULL || store->buckets == NULL || length == NULL) return NULL;
    *length = 0;
    pthread_mutex_lock(&store->mutex);
    size_t pairs = 0, bytes = 0;
    for (size_t i = 0; i < store->capacity; ++i) {
        for (entry_t *entry = store->buckets[i]; entry != NULL; entry = entry->next) {
            size_t line = strlen(entry->key) + strlen(entry->value) + 6;
            if (line > SIZE_MAX - bytes) goto failed;
            bytes += line;
            ++pairs;
        }
    }
    char header[64];
    int header_size = snprintf(header, sizeof(header), "storing %zu key-value pairs:\n", pairs);
    if (header_size < 0 || (size_t) header_size >= sizeof(header) ||
        bytes > SIZE_MAX - (size_t) header_size - 1) goto failed;
    bytes += (size_t) header_size;
    char *snapshot = malloc(bytes + 1);
    if (snapshot == NULL) goto failed;
    memcpy(snapshot, header, (size_t) header_size);
    size_t used = (size_t) header_size;
    for (size_t i = 0; i < store->capacity; ++i) {
        for (entry_t *entry = store->buckets[i]; entry != NULL; entry = entry->next) {
            int written = snprintf(snapshot + used, bytes + 1 - used,
                                   "%s --> %s\n", entry->key, entry->value);
            if (written < 0 || (size_t) written >= bytes + 1 - used) {
                free(snapshot);
                goto failed;
            }
            used += (size_t) written;
        }
    }
    snapshot[used] = 0;
    *length = used;
    pthread_mutex_unlock(&store->mutex);
    return snapshot;
failed:
    pthread_mutex_unlock(&store->mutex);
    return NULL;
}
