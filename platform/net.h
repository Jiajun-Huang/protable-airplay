










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
/**
 * @brief net_init.
 * @return Function result.
 */
int net_init(void);
/* Release process-wide network resources after every service has stopped. */
/**
 * @brief net_deinit.
 */
void net_deinit(void);

/* Create a TCP listener. Port 0 requests a platform-assigned port. */
/**
 * @brief net_tcp_listen.
 * @param sock Parameter named sock.
 * @param ip Parameter named ip.
 * @param port Parameter named port.
 * @return Function result.
 */
int net_tcp_listen(net_socket_t *sock, const char *ip, uint16_t port);
/* Accept one TCP peer, honoring the common timeout contract below. */
/**
 * @brief net_tcp_accept.
 * @param listener Parameter named listener.
 * @param client Parameter named client.
 * @param peer Parameter named peer.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int net_tcp_accept(net_socket_t *listener, net_socket_t *client, net_addr_t *peer, int timeout_ms);
/* Bind a UDP socket on all interfaces. Port 0 requests an assigned port. */
/**
 * @brief net_udp_bind.
 * @param sock Parameter named sock.
 * @param port Parameter named port.
 * @return Function result.
 */
int net_udp_bind(net_socket_t *sock, uint16_t port);

/* timeout_ms: 0 = try immediately, -1 = wait forever, positive = bounded wait.
 * Receives return bytes, NET_TIMEOUT, or NET_ERROR. TCP 0 means peer closed;
 * UDP 0 means an empty datagram; oversized datagrams are discarded and return
 * NET_TIMEOUT. TCP send returns 0 only after all bytes send.
 * On a send error/timeout the stream may be partially written: close it.
 */
/* Receive bytes from a connected TCP stream. */
/**
 * @brief net_tcp_recv.
 * @param sock Parameter named sock.
 * @param data Parameter named data.
 * @param capacity Parameter named capacity.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int net_tcp_recv(net_socket_t *sock, void *data, size_t capacity, int timeout_ms);
/* Send an entire TCP buffer or fail if the deadline expires. */
/**
 * @brief net_tcp_send_all.
 * @param sock Parameter named sock.
 * @param data Parameter named data.
 * @param length Parameter named length.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int net_tcp_send_all(net_socket_t *sock, const void *data, size_t length, int timeout_ms);
/* Receive one UDP datagram and optionally return its source endpoint. */
/**
 * @brief net_udp_recv.
 * @param sock Parameter named sock.
 * @param data Parameter named data.
 * @param capacity Parameter named capacity.
 * @param peer Parameter named peer.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int net_udp_recv(net_socket_t *sock, void *data, size_t capacity, net_addr_t *peer, int timeout_ms);
/* Send one UDP datagram to the supplied endpoint. */
/**
 * @brief net_udp_send.
 * @param sock Parameter named sock.
 * @param data Parameter named data.
 * @param length Parameter named length.
 * @param peer Parameter named peer.
 * @return Function result.
 */
int net_udp_send(net_socket_t *sock, const void *data, size_t length, const net_addr_t *peer);

/* Join group and select the same outgoing interface; multicast TTL is 255. */
/**
 * @brief net_udp_join.
 * @param sock Parameter named sock.
 * @param group Parameter named group.
 * @param interface_ip Parameter named interface_ip.
 * @return Function result.
 */
int net_udp_join(net_socket_t *sock, const char *group, const char *interface_ip);

/* Invalid sockets are ignored. Returns ready count, 0 timeout, or NET_ERROR. */
/**
 * @brief net_wait.
 * @param sockets Parameter named sockets.
 * @param count Parameter named count.
 * @param ready Parameter named ready.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int net_wait(const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms);
/* Close a socket and restore it to NET_SOCKET_INIT state. */
/**
 * @brief net_close.
 * @param sock Parameter named sock.
 */
void net_close(net_socket_t *sock);

#endif
