#include "socket_layer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int64_t udp_deadline(int timeout_ms)
{
    int64_t now = monotonic_ms();
    return now < 0 ? -1 : now + timeout_ms;
}

int udp_open(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        /* macOS' default send buffer can be smaller than a valid UDP payload. */
        int buffer_size = 256 * 1024;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) != 0 ||
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) != 0) {
            close(fd);
            return -1;
        }
        int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

int udp_address(const char *ip, unsigned short port, struct sockaddr_in *address)
{
    if (ip == NULL || address == NULL || port == 0) return -1;
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    return inet_pton(AF_INET, ip, &address->sin_addr) == 1 ? 0 : -1;
}

int udp_bind(const char *ip, unsigned short port)
{
    struct sockaddr_in address;
    if (udp_address(ip, port, &address) != 0) return -1;
    int fd = udp_open();
    if (fd >= 0 && bind(fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int udp_same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family &&
           a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

int udp_send(int fd, const void *data, size_t length, const struct sockaddr_in *peer)
{
    ssize_t sent;
    do {
        sent = sendto(fd, data, length, 0, (const struct sockaddr *) peer, sizeof(*peer));
    } while (sent < 0 && errno == EINTR);
    return sent >= 0 && (size_t) sent == length ? 0 : -1;
}

ssize_t udp_receive_until(int fd, void *buffer, size_t capacity,
                          struct sockaddr_in *peer, int64_t deadline)
{
    for (;;) {
        int64_t now = monotonic_ms();
        if (now < 0) return -1;
        int64_t remaining = deadline - now;
        if (remaining <= 0) {
            errno = EAGAIN;
            return -1;
        }
        struct pollfd ready = {.fd = fd, .events = POLLIN};
        int wait_ms = remaining > INT_MAX ? INT_MAX : (int) remaining;
        int status = poll(&ready, 1, wait_ms);
        if (status < 0 && errno == EINTR) continue;
        if (status <= 0) {
            if (status == 0) errno = EAGAIN;
            return -1;
        }
        if (!(ready.revents & POLLIN)) {
            errno = EIO;
            return -1;
        }
        struct sockaddr_in sender = {0};
        socklen_t sender_size = sizeof(sender);
        ssize_t size = recvfrom(fd, buffer, capacity, 0,
                                (struct sockaddr *) &sender, &sender_size);
        if (size < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (size >= 0 && peer != NULL) *peer = sender;
        return size;
    }
}
