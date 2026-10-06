#include "args.h"
#include "dkvs.h"
#include "socket_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *config = "servers.txt";
    int timeout_ms = DEFAULT_TIMEOUT_MS;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) goto usage;
        if (strcmp(argv[i], "-c") == 0) config = argv[i + 1];
        else if (strcmp(argv[i], "-t") == 0) {
            uint64_t number;
            if (parse_unsigned(argv[i + 1], 60000, &number) != 0 || number == 0) goto usage;
            timeout_ms = (int) number;
        } else goto usage;
    }
    ring_t ring;
    if (ring_load(&ring, config) != 0) {
        fprintf(stderr, "Could not load ring configuration: %s\n", config);
        return 2;
    }
    puts("Ring nodes:");
    ring_print(&ring);
    int status = 0;
    for (size_t i = 0; i < ring.count; ++i) {
        size_t previous = 0;
        while (previous < i && !udp_same_peer(&ring.nodes[previous].address, &ring.nodes[i].address))
            ++previous;
        if (previous < i) continue;
        const node_t *node = &ring.nodes[i];
        printf("%s:%u:\n", node->ip, node->port);
        /* A new socket for each server isolates delayed replies from old dumps. */
        int fd = udp_open();
        int64_t deadline = udp_deadline(timeout_ms);
        if (fd < 0 || deadline < 0 || udp_send(fd, "", 0, &node->address) != 0) {
            if (fd >= 0) close(fd);
            status = 1;
            continue;
        }
        int received = 0;
        for (;;) {
            unsigned char buffer[MAX_MESSAGE + 1];
            struct sockaddr_in sender;
            ssize_t length = udp_receive_until(fd, buffer, sizeof(buffer), &sender, deadline);
            if (length < 0) break;
            if (!udp_same_peer(&sender, &node->address) || length == 0 ||
                memchr(buffer, 0, (size_t) length) != NULL) continue;
            received = 1;
            if (fwrite(buffer, 1, (size_t) length, stdout) != (size_t) length) status = 1;
        }
        if (!received) {
            fprintf(stderr, "No dump reply from %s:%u\n", node->ip, node->port);
            status = 1;
        }
        close(fd);
    }
    ring_destroy(&ring);
    return status;
usage:
    fprintf(stderr, "Usage: %s [-c CONFIG] [-t TIMEOUT_MS]\n", argv[0]);
    return 2;
}
