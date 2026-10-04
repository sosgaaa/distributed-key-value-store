#include "dkvs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    unsigned char bytes[MAX_ELEMENT];
    size_t length;
    size_t count;
} vote_t;

static int parse_count(const char *text, size_t *result)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != 0 || value == 0 || value > MAX_NODES) return -1;
    *result = (size_t) value;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s {put|get|ring} [-c CONFIG] [-n N] [-r R] [-w W] -- [KEY [VALUE]]\n",
            program);
}

static ssize_t exchange(int socket_fd, const node_t *node, const void *request,
                        size_t request_length, unsigned char *response)
{
    ssize_t sent = sendto(socket_fd, request, request_length, 0,
                          (const struct sockaddr *) &node->address, sizeof(node->address));
    if (sent < 0 || (size_t) sent != request_length) return -1;
    for (;;) {
        struct sockaddr_in sender = {0};
        socklen_t sender_size = sizeof(sender);
        ssize_t received = recvfrom(socket_fd, response, MAX_MESSAGE + 1, 0,
                                    (struct sockaddr *) &sender, &sender_size);
        if (received < 0) return -1;
        if (sender.sin_addr.s_addr == node->address.sin_addr.s_addr &&
            sender.sin_port == node->address.sin_port) return received;
        /* A delayed response to an earlier request is never counted. */
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    const char *command = argv[1];
    const char *config = "servers.txt";
    size_t n = 0, r = 1, w = 1;
    int i = 2;
    while (i < argc && strcmp(argv[i], "--") != 0) {
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        const char *flag = argv[i++];
        const char *value = argv[i++];
        if (strcmp(flag, "-c") == 0) config = value;
        else if (strcmp(flag, "-n") == 0 && parse_count(value, &n) == 0) {}
        else if (strcmp(flag, "-r") == 0 && parse_count(value, &r) == 0) {}
        else if (strcmp(flag, "-w") == 0 && parse_count(value, &w) == 0) {}
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (i < argc && strcmp(argv[i], "--") == 0) ++i;
    int operand_count = argc - i;
    int is_put = strcmp(command, "put") == 0;
    int is_get = strcmp(command, "get") == 0;
    int is_ring = strcmp(command, "ring") == 0;
    if ((!is_put && !is_get && !is_ring) ||
        (is_put && operand_count != 2) || (is_get && operand_count != 1) ||
        (is_ring && operand_count != 0)) {
        usage(argv[0]);
        return 2;
    }
    if (!is_ring && (argv[i][0] == 0 || strlen(argv[i]) > MAX_ELEMENT ||
                     (is_put && strlen(argv[i + 1]) > MAX_ELEMENT))) {
        fprintf(stderr, "Key or value is empty/too long\n");
        return 2;
    }

    ring_t ring;
    if (ring_load(&ring, config) != 0) {
        fprintf(stderr, "Could not load ring configuration: %s\n", config);
        return 2;
    }
    if (is_ring) {
        for (size_t j = 0; j < ring.count; ++j) {
            const node_t *node = &ring.nodes[j];
            printf("%s:%u vnode=%u\n", node->ip, node->port, node->virtual_id);
        }
        ring_destroy(&ring);
        return 0;
    }
    if (n == 0) n = ring.server_count;
    if (n > ring.server_count || r > n || w > n) {
        fprintf(stderr, "Require 1 <= R,W <= N <= physical server count\n");
        ring_destroy(&ring);
        return 2;
    }
    node_t *selected = calloc(n, sizeof(*selected));
    vote_t *votes = calloc(n, sizeof(*votes));
    if (selected == NULL || votes == NULL || ring_select(&ring, argv[i], n, selected) != n) {
        fprintf(stderr, "Could not select servers\n");
        free(selected);
        free(votes);
        ring_destroy(&ring);
        return 1;
    }
    ring_destroy(&ring);

    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        free(selected);
        free(votes);
        return 1;
    }
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 300000};
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        perror("setsockopt");
        close(socket_fd);
        free(selected);
        free(votes);
        return 1;
    }
    unsigned char request[MAX_MESSAGE];
    size_t key_length = strlen(argv[i]);
    memcpy(request, argv[i], key_length);
    size_t request_length = key_length;
    if (is_put) {
        size_t value_length = strlen(argv[i + 1]);
        request[key_length] = 0;
        memcpy(request + key_length + 1, argv[i + 1], value_length);
        request_length += value_length + 1;
    }

    size_t acknowledgements = 0, distinct_votes = 0;
    int success = 0;
    for (size_t j = 0; j < n; ++j) {
        unsigned char response[MAX_MESSAGE + 1];
        ssize_t length = exchange(socket_fd, &selected[j], request, request_length, response);
        if (length < 0) continue;
        if (is_put) {
            if (length == 0 && ++acknowledgements >= w) {
                success = 1;
                break;
            }
            continue;
        }
        if (length > MAX_ELEMENT || (length == 1 && response[0] == 0) ||
            (length > 0 && memchr(response, 0, (size_t) length) != NULL)) continue;
        size_t k = 0;
        while (k < distinct_votes &&
               (votes[k].length != (size_t) length ||
                memcmp(votes[k].bytes, response, (size_t) length) != 0)) ++k;
        if (k == distinct_votes) {
            memcpy(votes[k].bytes, response, (size_t) length);
            votes[k].length = (size_t) length;
            ++distinct_votes;
        }
        if (++votes[k].count >= r) {
            printf("OK %.*s\n", (int) length, (const char *) response);
            success = 1;
            break;
        }
    }
    if (is_put) puts(success ? "OK" : "FAIL");
    else if (!success) puts("FAIL");
    close(socket_fd);
    free(selected);
    free(votes);
    return success ? 0 : 1;
}
