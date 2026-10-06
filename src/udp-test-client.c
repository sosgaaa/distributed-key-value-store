#include "args.h"
#include "config.h"
#include "socket_layer.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    uint64_t port = 1234, number;
    if (argc > 3 || (argc == 3 &&
        (parse_unsigned(argv[2], 65535, &port) != 0 || port == 0))) return 2;
    char input[64];
    if (fgets(input, sizeof(input), stdin) == NULL) return 2;
    for (char *p = input; *p != 0; ++p) if (*p == '\n') { *p = 0; break; }
    if (parse_unsigned(input, UINT32_MAX - 1, &number) != 0) return 2;
    struct sockaddr_in address;
    if (udp_address(argc > 1 ? argv[1] : "127.0.0.1", (unsigned short) port, &address) != 0)
        return 2;
    int fd = udp_open();
    if (fd < 0) return 1;
    uint32_t request = htonl((uint32_t) number);
    int result = 1;
    if (udp_send(fd, &request, sizeof(request), &address) == 0) {
        int64_t deadline = udp_deadline(DEFAULT_TIMEOUT_MS);
        for (;;) {
            uint32_t response[2];
            struct sockaddr_in peer;
            ssize_t size = udp_receive_until(fd, response, sizeof(response), &peer, deadline);
            if (size < 0) break;
            if (!udp_same_peer(&peer, &address) || size != sizeof(uint32_t)) continue;
            if (ntohl(response[0]) == number + 1) {
                printf("%u\n", ntohl(response[0]));
                result = 0;
            }
            break;
        }
    }
    close(fd);
    return result;
}
