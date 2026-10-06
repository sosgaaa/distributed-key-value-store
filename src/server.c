#include "dkvs.h"
#include "args.h"
#include "socket_layer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int socket_fd;
    store_t *store;
    struct sockaddr_in peer;
    socklen_t peer_size;
    unsigned char payload[MAX_MESSAGE + 1];
    size_t length;
} request_t;

static volatile sig_atomic_t stopping = 0;
static pthread_mutex_t workers_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workers_finished = PTHREAD_COND_INITIALIZER;
static size_t active_workers = 0;

static void stop_server(int signal_number)
{
    (void) signal_number;
    stopping = 1;
}

static void reply(const request_t *request, const void *data, size_t length)
{
    (void) udp_send(request->socket_fd, data, length, &request->peer);
}

static void handle_request(request_t *request)
{
    const unsigned char failure = 0;
    if (request->length == 0) {
        size_t length;
        char *snapshot = store_snapshot(request->store, &length);
        if (snapshot == NULL) return;
        /* Snapshot is stable; no table mutex is held while sending packets. */
        for (size_t offset = 0; offset < length;) {
            size_t chunk = length - offset;
            if (chunk > DUMP_CHUNK_SIZE) chunk = DUMP_CHUNK_SIZE;
            if (udp_send(request->socket_fd, snapshot + offset, chunk, &request->peer) != 0) break;
            offset += chunk;
        }
        free(snapshot);
        return;
    }
    if (request->length > MAX_MESSAGE) {
        reply(request, &failure, 1);
        return;
    }
    unsigned char *separator = memchr(request->payload, 0, request->length);
    if (separator == NULL) {
        if (request->length > MAX_ELEMENT) {
            reply(request, &failure, 1);
            return;
        }
        request->payload[request->length] = 0;
        char *value = store_get(request->store, (char *) request->payload);
        if (value == NULL) {
            reply(request, &failure, 1);
        } else {
            reply(request, value, strlen(value));
            free(value);
        }
        return;
    }

    size_t key_length = (size_t) (separator - request->payload);
    size_t value_length = request->length - key_length - 1;
    if (key_length == 0 || key_length > MAX_ELEMENT || value_length > MAX_ELEMENT ||
        memchr(separator + 1, 0, value_length) != NULL) {
        reply(request, "", 0);
        return;
    }
    request->payload[request->length] = 0;
    if (store_put(request->store, (char *) request->payload, (char *) separator + 1) != 0) {
        reply(request, "", 0);
    } else {
        reply(request, &failure, 1);
    }
}

static void *worker(void *argument)
{
    request_t *request = argument;
    handle_request(request);
    free(request);
    pthread_mutex_lock(&workers_mutex);
    --active_workers;
    pthread_cond_signal(&workers_finished);
    pthread_mutex_unlock(&workers_mutex);
    return NULL;
}

static int parse_port(const char *text, unsigned short *port)
{
    uint64_t value;
    if (parse_unsigned(text, 65535, &value) != 0 || value == 0) return -1;
    *port = (unsigned short) value;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3 || (argc - 3) % 2 != 0) {
        fprintf(stderr, "Usage: %s IP PORT [KEY VALUE ...]\n", argv[0]);
        return 2;
    }
    struct sockaddr_in bind_address = {0};
    bind_address.sin_family = AF_INET;
    unsigned short port;
    if (parse_port(argv[2], &port) != 0 ||
        inet_pton(AF_INET, argv[1], &bind_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address or port\n");
        return 2;
    }
    bind_address.sin_port = htons(port);

    store_t store = {0};
    if (store_init(&store, 1024) != 0) {
        fprintf(stderr, "Could not initialize the store\n");
        return 1;
    }
    for (int i = 3; i < argc; i += 2) {
        if (argv[i][0] == 0 || strlen(argv[i]) > MAX_ELEMENT ||
            strlen(argv[i + 1]) > MAX_ELEMENT ||
            store_put(&store, argv[i], argv[i + 1]) != 0) {
            fprintf(stderr, "Invalid initial key/value pair\n");
            store_destroy(&store);
            return 2;
        }
    }
    int socket_fd = udp_bind(argv[1], port);
    if (socket_fd < 0) {
        perror("socket/bind");
        if (socket_fd >= 0) close(socket_fd);
        store_destroy(&store);
        return 1;
    }
    struct sigaction action = {0};
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) {
        close(socket_fd);
        store_destroy(&store);
        return 1;
    }
    int status = 0;

    while (!stopping) {
        request_t *request = calloc(1, sizeof(*request));
        if (request == NULL) { status = 1; break; }
        request->socket_fd = socket_fd;
        request->store = &store;
        request->peer_size = sizeof(request->peer);
        ssize_t length = udp_receive_until(socket_fd, request->payload, sizeof(request->payload),
                                          &request->peer, udp_deadline(100));
        if (length < 0) {
            free(request);
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("recvfrom");
            status = 1;
            break;
        }
        request->length = (size_t) length;
        pthread_mutex_lock(&workers_mutex);
        if (active_workers >= 128) {
            pthread_mutex_unlock(&workers_mutex);
            handle_request(request);
            free(request);
            continue;
        }
        ++active_workers;
        pthread_mutex_unlock(&workers_mutex);
        pthread_t thread;
        if (pthread_create(&thread, NULL, worker, request) != 0) {
            handle_request(request);
            free(request);
            pthread_mutex_lock(&workers_mutex);
            --active_workers;
            pthread_mutex_unlock(&workers_mutex);
        } else {
            pthread_detach(thread);
        }
    }

    pthread_mutex_lock(&workers_mutex);
    while (active_workers != 0) pthread_cond_wait(&workers_finished, &workers_mutex);
    pthread_mutex_unlock(&workers_mutex);
    close(socket_fd);
    store_destroy(&store);
    return status;
}
