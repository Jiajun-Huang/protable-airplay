#ifndef AIRPLAY_NET_H
#define AIRPLAY_NET_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uintptr_t handle;
    uint16_t port;
} net_socket_t;

typedef struct {
    char ip[16];
    uint16_t port;
} net_addr_t;

#define NET_SOCKET_INIT { UINTPTR_MAX, 0 }
#define NET_ERROR (-1)
#define NET_TIMEOUT (-2)

/* Initialize once before starting services; deinitialize after all sockets close. */
int net_init(void);
void net_deinit(void);

/* Outputs become valid only on success. Port 0 requests an assigned port. */
int net_tcp_listen(net_socket_t *sock, const char *ip, uint16_t port);
int net_tcp_accept(net_socket_t *listener, net_socket_t *client,
                   net_addr_t *peer, int timeout_ms);
int net_udp_bind(net_socket_t *sock, uint16_t port);

/* timeout_ms: 0 = try immediately, -1 = wait forever, positive = bounded wait.
 * Receives return bytes, NET_TIMEOUT, or NET_ERROR. TCP 0 means peer closed;
 * UDP 0 means an empty datagram; oversized datagrams are discarded and return
 * NET_TIMEOUT. TCP send returns 0 only after all bytes send.
 * On a send error/timeout the stream may be partially written: close it.
 */
int net_tcp_recv(net_socket_t *sock, void *data, size_t capacity, int timeout_ms);
int net_tcp_send_all(net_socket_t *sock, const void *data, size_t length, int timeout_ms);
int net_udp_recv(net_socket_t *sock, void *data, size_t capacity,
                 net_addr_t *peer, int timeout_ms);
int net_udp_send(net_socket_t *sock, const void *data, size_t length,
                 const net_addr_t *peer);

/* Join group and select the same outgoing interface; multicast TTL is 255. */
int net_udp_join(net_socket_t *sock, const char *group, const char *interface_ip);

/* Invalid sockets are ignored. Returns ready count, 0 timeout, or NET_ERROR. */
int net_wait(const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms);
void net_close(net_socket_t *sock);

#endif
