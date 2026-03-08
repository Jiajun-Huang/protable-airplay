#ifndef UDP_IF_H
#define UDP_IF_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief UDP/IP abstraction layer
 * Platform-specific UDP implementations should implement these interfaces.
 * RTP receiver and mDNS services use this interface.
 */

/**
 * @brief UDP socket structure - caller-allocated
 */
typedef struct udp_socket
{
    uint64_t socket; // Socket handle (SOCKET on Windows, int fd on Unix)
    uint16_t port;   // Local bound port number
} udp_socket_t;

/**
 * @brief Create and bind a UDP socket on a port
 * Socket is set to non-blocking mode and suitable for both send and receive.
 * Enables SO_REUSEADDR for multicast compatibility.
 *
 * @param sock Pointer to pre-allocated UDP socket struct (caller-allocated)
 * @param port Local port to bind to (0 for any available port)
 * @return 0 on success, negative on error
 */
int udp_create(udp_socket_t *sock, uint16_t port);

/**
 * @brief Poll UDP socket for readable data
 * Use this before calling udp_receive() to check for activity.
 *
 * @param socket UDP socket
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = block forever)
 * @return >0 if data available, 0 on timeout, negative on error
 */
int udp_poll(udp_socket_t *socket, int timeout_ms);

/**
 * @brief Send UDP data to a destination
 * Works for unicast, broadcast, and multicast addresses.
 *
 * @param socket UDP socket
 * @param data Bytes to send
 * @param len Number of bytes to send
 * @param dest_ip Destination IP address string (e.g., "192.168.1.1")
 * @param dest_port Destination port
 * @return Number of bytes sent, or negative on error
 */
int udp_send(udp_socket_t *socket, const uint8_t *data, size_t len,
             const char *dest_ip, uint16_t dest_port);

/**
 * @brief Receive UDP data from socket
 * Provides sender information for response.
 *
 * @param socket UDP socket
 * @param buffer Output buffer for received data
 * @param buffer_size Size of output buffer
 * @param src_ip Output buffer for sender IP address (min 16 bytes, can be NULL)
 * @param src_port Output pointer for sender port (can be NULL)
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking)
 * @return Number of bytes received, 0 on timeout, negative on error
 */
int udp_receive(udp_socket_t *socket, uint8_t *buffer, size_t buffer_size,
                char *src_ip, uint16_t *src_port, int timeout_ms);

/**
 * @brief Join a multicast group
 * Allows receiving multicast packets sent to the group.
 *
 * @param socket UDP socket
 * @param group_ip Multicast group IP string (e.g., "224.0.0.251" for mDNS)
 * @param interface_ip Local interface IP string (NULL for default/all interfaces)
 * @return 0 on success, negative on error
 */
int udp_join_multicast(udp_socket_t *socket, const char *group_ip, const char *interface_ip);

/**
 * @brief Leave a multicast group
 *
 * @param socket UDP socket
 * @param group_ip Multicast group IP string to leave
 * @param interface_ip Local interface IP string (NULL for default)
 * @return 0 on success, negative on error
 */
int udp_leave_multicast(udp_socket_t *socket, const char *group_ip, const char *interface_ip);

/**
 * @brief Set multicast TTL (Time To Live)
 * Controls how many network hops multicast packets can traverse.
 *
 * @param socket UDP socket
 * @param ttl TTL value (1 = local network, 255 = global)
 * @return 0 on success, negative on error
 */
int udp_set_multicast_ttl(udp_socket_t *socket, int ttl);

/**
 * @brief Set outbound multicast interface
 * Forces multicast packets to leave through a specific local IPv4 interface.
 *
 * @param socket UDP socket
 * @param interface_ip Local interface IPv4 string (e.g., "10.0.0.178")
 * @return 0 on success, negative on error
 */
int udp_set_multicast_interface(udp_socket_t *socket, const char *interface_ip);

/**
 * @brief Enable broadcast sending
 * Required before sending to broadcast addresses like 255.255.255.255
 *
 * @param socket UDP socket
 * @return 0 on success, negative on error
 */
int udp_enable_broadcast(udp_socket_t *socket);

/**
 * @brief Get the actual bound port
 * Useful when creating socket with port 0 (auto-assign).
 *
 * @param socket UDP socket
 * @return Port number, or 0 on error
 */
uint16_t udp_get_port(udp_socket_t *socket);

/**
 * @brief Close UDP socket
 *
 * @param socket UDP socket to close
 */
void udp_close(udp_socket_t *socket);

#endif // UDP_IF_H
