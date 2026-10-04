#include "dkvs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t hash_key(const char *key, size_t capacity)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *) key; *p != 0; ++p) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return (size_t) (hash % capacity);
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
    if (store == NULL || key == NULL || value == NULL) return -1;
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
    if (store == NULL || key == NULL) return NULL;
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
