#ifndef AIRPLAY_NET_H
#define AIRPLAY_NET_H

#include <stddef.h>
#include <stdint.h>

/* Portable socket interface used by every shared protocol and service module.
 * Backends translate these contracts to Winsock, BSD sockets, or lwIP. */

/* Platform socket handle plus the bound local port. */
typedef struct
{
    uintptr_t handle;
    uint16_t port;
} net_socket_t;

/* IPv4 endpoint written as dotted-decimal text and a host-order port. */
typedef struct
{
    char ip[16];
    uint16_t port;
} net_addr_t;

#define NET_SOCKET_INIT {UINTPTR_MAX, 0}
#define NET_ERROR       (-1)
#define NET_TIMEOUT     (-2)

/* Initialize once before starting services; deinitialize after all sockets close. */
int net_init(void);
/* Release process-wide network resources after every service has stopped. */
void net_deinit(void);

/* Create a TCP listener. Port 0 requests a platform-assigned port. */
int net_tcp_listen(net_socket_t *sock, const char *ip, uint16_t port);
/* Accept one TCP peer, honoring the common timeout contract below. */
int net_tcp_accept(net_socket_t *listener, net_socket_t *client, net_addr_t *peer, int timeout_ms);
/* Bind a UDP socket on all interfaces. Port 0 requests an assigned port. */
int net_udp_bind(net_socket_t *sock, uint16_t port);

/* timeout_ms: 0 = try immediately, -1 = wait forever, positive = bounded wait.
 * Receives return bytes, NET_TIMEOUT, or NET_ERROR. TCP 0 means peer closed;
 * UDP 0 means an empty datagram; oversized datagrams are discarded and return
 * NET_TIMEOUT. TCP send returns 0 only after all bytes send.
 * On a send error/timeout the stream may be partially written: close it.
 */
/* Receive bytes from a connected TCP stream. */
int net_tcp_recv(net_socket_t *sock, void *data, size_t capacity, int timeout_ms);
/* Send an entire TCP buffer or fail if the deadline expires. */
int net_tcp_send_all(net_socket_t *sock, const void *data, size_t length, int timeout_ms);
/* Receive one UDP datagram and optionally return its source endpoint. */
int net_udp_recv(net_socket_t *sock, void *data, size_t capacity, net_addr_t *peer, int timeout_ms);
/* Send one UDP datagram to the supplied endpoint. */
int net_udp_send(net_socket_t *sock, const void *data, size_t length, const net_addr_t *peer);

/* Join group and select the same outgoing interface; multicast TTL is 255. */
int net_udp_join(net_socket_t *sock, const char *group, const char *interface_ip);

/* Invalid sockets are ignored. Returns ready count, 0 timeout, or NET_ERROR. */
int net_wait(const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms);
/* Close a socket and restore it to NET_SOCKET_INIT state. */
void net_close(net_socket_t *sock);

#endif
