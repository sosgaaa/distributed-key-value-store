#include "args.h"
#include "socket_layer.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void) signal_number; stopping = 1; }

int main(int argc, char **argv)
{
    uint64_t port = 1234;
    if (argc > 3 || (argc == 3 &&
        (parse_unsigned(argv[2], 65535, &port) != 0 || port == 0))) return 2;
    int fd = udp_bind(argc > 1 ? argv[1] : "127.0.0.1", (unsigned short) port);
    if (fd < 0) return 1;
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) {
        close(fd);
        return 1;
    }
    int result = 0;
    while (!stopping) {
        uint32_t request[2];
        struct sockaddr_in peer;
        ssize_t size = udp_receive_until(fd, request, sizeof(request), &peer, udp_deadline(100));
        if (size < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            result = 1;
            break;
        }
        if (size != sizeof(uint32_t) || ntohl(request[0]) == UINT32_MAX) continue;
        uint32_t response = htonl(ntohl(request[0]) + 1);
        (void) udp_send(fd, &response, sizeof(response), &peer);
    }
    close(fd);
    return result;
}
