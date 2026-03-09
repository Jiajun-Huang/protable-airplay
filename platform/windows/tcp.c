#include "../tcp_if.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/**
 * @brief Windows TCP implementation using Winsock2
 */

static int winsock_initialized = 0;

static int ensure_winsock_init(void)
{
    if (winsock_initialized)
        return 0;

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        printf("[TCP] WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

    winsock_initialized = 1;
    return 0;
}

int tcp_create_server(tcp_socket_t *server, uint16_t port, const char *listen_ip)
{
    if (!server)
        return -1;

    if (ensure_winsock_init() != 0)
        return -1;

    memset(server, 0, sizeof(tcp_socket_t));

    // Create listening socket
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET)
    {
        printf("[TCP] socket() failed: %d\n", WSAGetLastError());
        return -1;
    }

    // Allow reuse of address
    int reuse = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
    {
        printf("[TCP] setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
    }

    // Set to non-blocking mode
    u_long mode = 1;
    if (ioctlsocket(listen_sock, FIONBIO, &mode) == SOCKET_ERROR)
    {
        printf("[TCP] ioctlsocket(FIONBIO) failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        return -1;
    }

    // Bind to address and port
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (listen_ip == NULL || strcmp(listen_ip, "0.0.0.0") == 0)
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(listen_ip);
    }

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("[TCP] bind() failed on port %u: %d\n", port, WSAGetLastError());
        closesocket(listen_sock);
        return -1;
    }

    // Start listening
    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("[TCP] listen() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        return -1;
    }

    server->listen_socket = (uint64_t)listen_sock;
    printf("[TCP] Server listening on %s:%u\n", listen_ip ? listen_ip : "0.0.0.0", port);

    return 0;
}

int tcp_poll(tcp_socket_t *server, int timeout_ms)
{
    if (!server)
        return -1;

    SOCKET listen_sock = (SOCKET)server->listen_socket;
    if (listen_sock == INVALID_SOCKET)
        return -1;

    // Setup select() with listening socket and all client sockets
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_sock, &read_fds);

    for (int i = 0; i < server->client_count; i++)
    {
        SOCKET client_sock = (SOCKET)server->clients[i].socket;
        if (client_sock != INVALID_SOCKET)
        {
            FD_SET(client_sock, &read_fds);
        }
    }

    // Setup timeout
    struct timeval tv;
    struct timeval *tv_ptr = NULL;

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int result = select(0, &read_fds, NULL, NULL, tv_ptr);

    if (result == SOCKET_ERROR)
    {
        printf("[TCP] select() failed: %d\n", WSAGetLastError());
        return -1;
    }

    return result; // >0 if activity, 0 on timeout
}

int tcp_accept(tcp_socket_t *server, tcp_client_t *client, int timeout_ms)
{
    if (!server || !client)
        return -1;

    SOCKET listen_sock = (SOCKET)server->listen_socket;
    if (listen_sock == INVALID_SOCKET)
        return -1;

    // If timeout requested, use select first
    if (timeout_ms > 0)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int result = select(0, &read_fds, NULL, NULL, &tv);
        if (result == 0)
            return -1; // Timeout
        if (result == SOCKET_ERROR)
        {
            printf("[TCP] select() failed: %d\n", WSAGetLastError());
            return -1;
        }
    }

    // Accept connection
    struct sockaddr_in client_addr = {0};
    int addr_len = sizeof(client_addr);

    SOCKET client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock == INVALID_SOCKET)
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            printf("[TCP] accept() failed: %d\n", err);
        }
        return -1;
    }

    // Set client socket to non-blocking
    u_long mode = 1;
    ioctlsocket(client_sock, FIONBIO, &mode);

    // Fill client structure
    client->socket = (uint64_t)client_sock;
    strncpy_s(client->ip, sizeof(client->ip), inet_ntoa(client_addr.sin_addr), _TRUNCATE);
    client->port = ntohs(client_addr.sin_port);

    printf("[TCP] Accepted connection from %s:%u\n", client->ip, client->port);

    // client count
    if (server->client_count < TCP_MAX_CLIENTS)
    {
        server->clients[server->client_count++] = *client;
    }
    else
    {
        printf("[TCP] Maximum clients reached, closing new connection\n");
        closesocket(client_sock);
        return -1;
    }
    return 0;
}

int tcp_receive(tcp_client_t *client, uint8_t *data, size_t buffer_size, int timeout_ms)
{
    if (!client || !data || buffer_size == 0)
        return -1;

    SOCKET sock = (SOCKET)client->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    // If timeout requested, use select first
    if (timeout_ms > 0)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int result = select(0, &read_fds, NULL, NULL, &tv);
        if (result == 0)
            return -1; // Timeout
        if (result == SOCKET_ERROR)
            return -1;
    }

    // Receive data
    int bytes = recv(sock, (char *)data, (int)buffer_size, 0);

    if (bytes == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
            return -1; // No data available

        printf("[TCP] recv() failed: %d\n", err);
        return -1;
    }

    return bytes; // 0 = connection closed, >0 = bytes received
}

int tcp_send(tcp_client_t *client, const uint8_t *data, size_t len)
{
    if (!client || !data || len == 0)
        return -1;

    SOCKET sock = (SOCKET)client->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    int sent = send(sock, (const char *)data, (int)len, 0);

    if (sent == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        printf("[TCP] send() failed: %d\n", err);
        return -1;
    }

    return sent;
}

void tcp_close_server(tcp_socket_t *server)
{
    if (!server)
        return;

    // Close all client connections
    for (int i = 0; i < server->client_count; i++)
    {
        SOCKET sock = (SOCKET)server->clients[i].socket;
        if (sock != INVALID_SOCKET)
        {
            closesocket(sock);
            server->clients[i].socket = (uint64_t)INVALID_SOCKET;
        }
    }

    // Close listening socket
    SOCKET listen_sock = (SOCKET)server->listen_socket;
    if (listen_sock != INVALID_SOCKET)
    {
        closesocket(listen_sock);
        server->listen_socket = (uint64_t)INVALID_SOCKET;
    }

    server->client_count = 0;
}

void tcp_close_client(tcp_client_t *client)
{
    if (!client)
        return;

    SOCKET sock = (SOCKET)client->socket;
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        client->socket = (uint64_t)INVALID_SOCKET;
    }
}
