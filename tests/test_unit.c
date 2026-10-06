#include "args.h"
#include "dkvs.h"
#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_store(void)
{
    store_t store = {0};
    assert(store_init(&store, 1) == 0); /* Every key collides. */
    assert(store_put(&store, "a", "one") == 0);
    assert(store_put(&store, "b", "two") == 0);
    char *old = store_get(&store, "a");
    assert(old != NULL && strcmp(old, "one") == 0);
    assert(store_put(&store, "a", "new") == 0);
    assert(strcmp(old, "one") == 0); /* Owned copy survives replacement. */
    free(old);
    assert(store_put(&store, "empty", "") == 0);
    char *empty = store_get(&store, "empty");
    assert(empty != NULL && *empty == 0);
    free(empty);
    assert(store_get(&store, "missing") == NULL);
    assert(store_put(&store, "", "invalid") == -1);
    size_t length;
    char *snapshot = store_snapshot(&store, &length);
    assert(snapshot != NULL && length == strlen(snapshot));
    assert(strstr(snapshot, "storing 3 key-value pairs:\n") != NULL);
    assert(strstr(snapshot, "a --> new\n") != NULL);
    assert(strstr(snapshot, "b --> two\n") != NULL);
    assert(strstr(snapshot, "empty --> \n") != NULL);
    free(snapshot);
    store_destroy(&store);
    assert(store_put(&store, "a", "b") == -1);
}

typedef struct { store_t *store; unsigned int id; } work_t;
static void *write_keys(void *argument)
{
    work_t *work = argument;
    for (unsigned int i = 0; i < 100; ++i) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "thread-%u-key-%u", work->id, i);
        snprintf(value, sizeof(value), "%u", i);
        assert(store_put(work->store, key, value) == 0);
        char *copy = store_get(work->store, key);
        assert(copy != NULL && strcmp(copy, value) == 0);
        free(copy);
        size_t length;
        char *snapshot = store_snapshot(work->store, &length);
        assert(snapshot != NULL && length == strlen(snapshot));
        free(snapshot);
    }
    return NULL;
}

static void test_concurrent_store(void)
{
    store_t store = {0};
    assert(store_init(&store, 8) == 0);
    pthread_t threads[8];
    work_t work[8];
    for (unsigned int i = 0; i < 8; ++i) {
        work[i] = (work_t) {.store = &store, .id = i};
        assert(pthread_create(&threads[i], NULL, write_keys, &work[i]) == 0);
    }
    for (size_t i = 0; i < 8; ++i) assert(pthread_join(threads[i], NULL) == 0);
    size_t length;
    char *snapshot = store_snapshot(&store, &length);
    assert(snapshot != NULL && strstr(snapshot, "storing 800 key-value pairs:\n") == snapshot);
    free(snapshot);
    store_destroy(&store);
}

static void test_ring(void)
{
    char path[] = "/tmp/dkvs-ring-unit-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *config = fdopen(fd, "w");
    assert(config != NULL);
    fputs("127.0.0.1 1234 2\n127.0.0.1 1235 1\n127.0.0.1 1236 1\n", config);
    assert(fclose(config) == 0);
    ring_t ring;
    assert(ring_load(&ring, path) == 0);
    assert(ring.count == 4 && ring.server_count == 3);
    for (size_t i = 1; i < ring.count; ++i)
        assert(memcmp(ring.nodes[i - 1].digest, ring.nodes[i].digest, 20) <= 0);
    node_t selected[4];
    assert(ring_select(&ring, "key42", 4, selected) == 3);
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = i + 1; j < 3; ++j) assert(selected[i].port != selected[j].port);
    for (unsigned int i = 0; i < 200; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "key-%u", i);
        unsigned char sha[20];
        SHA1((unsigned char *) key, strlen(key), sha);
        size_t successor = 0;
        while (successor < ring.count && memcmp(ring.nodes[successor].digest, sha, 20) < 0)
            ++successor;
        if (successor == ring.count) successor = 0;
        assert(ring_select(&ring, key, 1, selected) == 1);
        assert(memcmp(selected[0].digest, ring.nodes[successor].digest, 20) == 0);
    }
    ring_destroy(&ring);
    assert(unlink(path) == 0);
}

static void test_numbers(void)
{
    uint64_t unsigned_value;
    int64_t signed_value;
    assert(parse_unsigned("18446744073709551615", UINT64_MAX, &unsigned_value) == 0);
    assert(unsigned_value == UINT64_MAX);
    assert(parse_unsigned("18446744073709551616", UINT64_MAX, &unsigned_value) == -1);
    assert(parse_unsigned("-1", UINT64_MAX, &unsigned_value) == -1);
    assert(parse_unsigned("+1", UINT64_MAX, &unsigned_value) == -1);
    assert(parse_unsigned("1junk", UINT64_MAX, &unsigned_value) == -1);
    assert(parse_position("-9223372036854775808", &signed_value) == 0);
    assert(signed_value == INT64_MIN);
    assert(parse_position("9223372036854775808", &signed_value) == -1);
    assert(parse_position("-", &signed_value) == -1);
}

int main(void)
{
    test_store();
    test_concurrent_store();
    test_ring();
    test_numbers();
    puts("4 unit test groups passed (store, concurrency, ring, numeric parsing)");
    return 0;
}
