#ifndef TCP_IF_H
#define TCP_IF_H

#include <stdint.h>
#include <stddef.h>

#define TCP_MAX_CLIENTS 4

/**
 * @brief TCP client structure
 */
typedef struct tcp_client
{
    uint64_t socket; // Socket handle (SOCKET on Windows, int fd on Unix)
    char ip[64];     // Client IP address as string
    uint16_t port;   // Client port number
} tcp_client_t;

/**
 * @brief TCP socket abstraction for AirPlay/RAOP
 * Platform-specific TCP implementations should implement these interfaces.
 * RTSP server uses this interface for control connections.
 */
typedef struct tcp_socket
{
    uint64_t listen_socket;                // Listening socket handle
    tcp_client_t clients[TCP_MAX_CLIENTS]; // Array of connected clients
    int client_count;                      // Number of active clients
} tcp_socket_t;

/**
 * @brief Create TCP server socket, bind to port, and start listening
 *
 * @param server Pointer to server structure (caller-allocated)
 * @param port Port number to listen on
 * @param listen_ip IP address to bind to (NULL or "0.0.0.0" for all interfaces)
 * @return 0 on success, negative on error
 */
int tcp_create_server(tcp_socket_t *server, uint16_t port, const char *listen_ip);

/**
 * @brief Poll server socket for readable events (pending connections or data)
 *
 * EFFICIENCY: For multi-client servers, use tcp_poll() to monitor listening socket
 * and all clients in ONE select() call. After tcp_poll() returns activity, call
 * tcp_accept() and tcp_receive() with timeout_ms=0 to avoid redundant select() calls.
 *
 * SIMPLE SERVERS: For single-client blocking servers, you can skip tcp_poll() and
 * call tcp_accept()/tcp_receive() directly with desired timeout.
 *
 * @param server Server socket
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = block forever)
 * @return >0 if activity detected, 0 on timeout, negative on error
 */
int tcp_poll(tcp_socket_t *server, int timeout_ms);

/**
 * @brief Accept a pending connection
 *
 * NOTE: After tcp_poll() returns activity, call this with timeout_ms=0 for efficiency.
 * If not using tcp_poll(), this function can be called directly with timeout to block.
 *
 * @param server Server socket
 * @param client Output parameter for accepted client (caller-allocated)
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, use 0 after tcp_poll)
 * @return 0 on success, negative on error or timeout
 */
int tcp_accept(tcp_socket_t *server, tcp_client_t *client, int timeout_ms);

/**
 * @brief Receive data from client
 *
 * NOTE: After tcp_poll() returns activity, call this with timeout_ms=0 for efficiency.
 * If not using tcp_poll(), this function can be called directly with timeout to block.
 *
 * @param client Client socket
 * @param data Output buffer for received data
 * @param buffer_size Size of output buffer
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, use 0 after tcp_poll)
 * @return Number of bytes received, 0 if connection closed, negative on error
 */
int tcp_receive(tcp_client_t *client, uint8_t *data, size_t buffer_size, int timeout_ms);

/**
 * @brief Send data to client
 *
 * @param client Client socket
 * @param data Data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent, or negative on error
 */
int tcp_send(tcp_client_t *client, const uint8_t *data, size_t len);

/**
 * @brief Close the TCP server and all client connections
 *
 * @param server Server socket to close
 */
void tcp_close_server(tcp_socket_t *server);

/**
 * @brief Close client connection
 *
 * @param client Client socket to close
 */
void tcp_close_client(tcp_client_t *client);

#endif // TCP_IF_H
