#ifndef DKVS_SOCKET_LAYER_H
#define DKVS_SOCKET_LAYER_H

#include <netinet/in.h>
#include <stdint.h>
#include <sys/types.h>

int udp_open(void);
int udp_address(const char *ip, unsigned short port, struct sockaddr_in *address);
int udp_bind(const char *ip, unsigned short port);
int udp_same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b);
int udp_send(int fd, const void *data, size_t length, const struct sockaddr_in *peer);
int64_t udp_deadline(int timeout_ms);
/* Absolute monotonic deadline: unrelated traffic cannot extend the timeout. */
ssize_t udp_receive_until(int fd, void *buffer, size_t capacity,
                          struct sockaddr_in *peer, int64_t deadline);

#endif
