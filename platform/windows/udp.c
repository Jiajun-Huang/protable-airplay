#include "udp_if.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/**
 * @brief Windows UDP implementation using Winsock2
 */

static int winsock_initialized = 0;

static int ensure_winsock_init(void)
{
    if (winsock_initialized)
        return 0;

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        printf("[UDP] WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

    winsock_initialized = 1;
    return 0;
}

int udp_create(udp_socket_t *sock, uint16_t port)
{
    if (!sock)
        return -1;

    if (ensure_winsock_init() != 0)
        return -1;

    memset(sock, 0, sizeof(udp_socket_t));

    // Create UDP socket
    SOCKET udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock == INVALID_SOCKET)
    {
        printf("[UDP] socket() failed: %d\n", WSAGetLastError());
        return -1;
    }

    // Enable SO_REUSEADDR for multicast compatibility
    int reuse = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
    }

    // Keep multicast traffic visible on the local host for debugging/capture.
    {
        int loop = 1;
        if (setsockopt(udp_sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop)) == SOCKET_ERROR)
        {
            printf("[UDP] setsockopt(IP_MULTICAST_LOOP) failed: %d\n", WSAGetLastError());
        }
    }

    {
        int ttl = 255;
        if (setsockopt(udp_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) == SOCKET_ERROR)
        {
            printf("[UDP] setsockopt(IP_MULTICAST_TTL) failed: %d\n", WSAGetLastError());
        }
    }

    // Set to non-blocking mode
    u_long mode = 1;
    if (ioctlsocket(udp_sock, FIONBIO, &mode) == SOCKET_ERROR)
    {
        printf("[UDP] ioctlsocket(FIONBIO) failed: %d\n", WSAGetLastError());
        closesocket(udp_sock);
        return -1;
    }

    // Bind to port
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("[UDP] bind() failed on port %u: %d\n", port, WSAGetLastError());
        closesocket(udp_sock);
        return -1;
    }

    // Get actual bound port (useful when port was 0)
    if (port == 0)
    {
        int addr_len = sizeof(addr);
        if (getsockname(udp_sock, (struct sockaddr *)&addr, &addr_len) == 0)
        {
            port = ntohs(addr.sin_port);
        }
    }

    sock->socket = (uint64_t)udp_sock;
    sock->port = port;

    printf("[UDP] Socket created and bound to port %u\n", port);

    return 0;
}

int udp_poll(udp_socket_t *socket, int timeout_ms)
{
    if (!socket)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    // Setup select()
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

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
        printf("[UDP] select() failed: %d\n", WSAGetLastError());
        return -1;
    }

    return result; // >0 if data available, 0 on timeout
}

int udp_send(udp_socket_t *socket, const uint8_t *data, size_t len,
             const char *dest_ip, uint16_t dest_port)
{
    if (!socket || !data || !dest_ip || len == 0)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    // Setup destination address
    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);

    // Convert string IP to network byte order
    if (InetPtonA(AF_INET, dest_ip, &dest_addr.sin_addr) != 1)
    {
        // Fallback to inet_addr for compatibility
        dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
        if (dest_addr.sin_addr.s_addr == INADDR_NONE)
        {
            printf("[UDP] Invalid destination IP: %s\n", dest_ip);
            return -1;
        }
    }

    int sent = sendto(sock, (const char *)data, (int)len, 0,
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        printf("[UDP] sendto() failed: %d\n", err);
        return -1;
    }

    return sent;
}

int udp_receive(udp_socket_t *socket, uint8_t *buffer, size_t buffer_size,
                char *src_ip, uint16_t *src_port, int timeout_ms)
{
    if (!socket || !buffer || buffer_size == 0)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
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
            return 0; // Timeout
        if (result == SOCKET_ERROR)
            return -1;
    }

    // Receive data
    struct sockaddr_in src_addr = {0};
    int addr_len = sizeof(src_addr);

    int bytes = recvfrom(sock, (char *)buffer, (int)buffer_size, 0,
                         (struct sockaddr *)&src_addr, &addr_len);

    if (bytes == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
            return 0; // No data available

        printf("[UDP] recvfrom() failed: %d\n", err);
        return -1;
    }

    // Fill source information if requested
    if (src_ip)
    {
        strncpy_s(src_ip, 16, inet_ntoa(src_addr.sin_addr), _TRUNCATE);
    }

    if (src_port)
    {
        *src_port = ntohs(src_addr.sin_port);
    }

    return bytes;
}

int udp_join_multicast(udp_socket_t *socket, const char *group_ip, const char *interface_ip)
{
    if (!socket || !group_ip)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    struct ip_mreq mreq = {0};

    // Set multicast group address
    if (InetPtonA(AF_INET, group_ip, &mreq.imr_multiaddr) != 1)
    {
        mreq.imr_multiaddr.s_addr = inet_addr(group_ip);
        if (mreq.imr_multiaddr.s_addr == INADDR_NONE)
        {
            printf("[UDP] Invalid multicast group IP: %s\n", group_ip);
            return -1;
        }
    }

    // Set interface address
    if (interface_ip && strlen(interface_ip) > 0)
    {
        if (InetPtonA(AF_INET, interface_ip, &mreq.imr_interface) != 1)
        {
            mreq.imr_interface.s_addr = inet_addr(interface_ip);
            if (mreq.imr_interface.s_addr == INADDR_NONE)
            {
                printf("[UDP] Invalid interface IP: %s\n", interface_ip);
                return -1;
            }
        }
    }
    else
    {
        mreq.imr_interface.s_addr = INADDR_ANY;
    }

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(IP_ADD_MEMBERSHIP) failed: %d\n", WSAGetLastError());
        return -1;
    }

    printf("[UDP] Joined multicast group %s\n", group_ip);

    return 0;
}

int udp_leave_multicast(udp_socket_t *socket, const char *group_ip, const char *interface_ip)
{
    if (!socket || !group_ip)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    struct ip_mreq mreq = {0};

    // Set multicast group address
    if (InetPtonA(AF_INET, group_ip, &mreq.imr_multiaddr) != 1)
    {
        mreq.imr_multiaddr.s_addr = inet_addr(group_ip);
        if (mreq.imr_multiaddr.s_addr == INADDR_NONE)
        {
            printf("[UDP] Invalid multicast group IP: %s\n", group_ip);
            return -1;
        }
    }

    // Set interface address
    if (interface_ip && strlen(interface_ip) > 0)
    {
        if (InetPtonA(AF_INET, interface_ip, &mreq.imr_interface) != 1)
        {
            mreq.imr_interface.s_addr = inet_addr(interface_ip);
            if (mreq.imr_interface.s_addr == INADDR_NONE)
            {
                printf("[UDP] Invalid interface IP: %s\n", interface_ip);
                return -1;
            }
        }
    }
    else
    {
        mreq.imr_interface.s_addr = INADDR_ANY;
    }

    if (setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(IP_DROP_MEMBERSHIP) failed: %d\n", WSAGetLastError());
        return -1;
    }

    printf("[UDP] Left multicast group %s\n", group_ip);

    return 0;
}

int udp_set_multicast_ttl(udp_socket_t *socket, int ttl)
{
    if (!socket || ttl < 0 || ttl > 255)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(IP_MULTICAST_TTL) failed: %d\n", WSAGetLastError());
        return -1;
    }

    return 0;
}

int udp_set_multicast_interface(udp_socket_t *socket, const char *interface_ip)
{
    struct in_addr iface;

    if (!socket || !interface_ip || interface_ip[0] == '\0')
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    if (InetPtonA(AF_INET, interface_ip, &iface) != 1)
    {
        iface.s_addr = inet_addr(interface_ip);
        if (iface.s_addr == INADDR_NONE)
        {
            printf("[UDP] Invalid multicast interface IP: %s\n", interface_ip);
            return -1;
        }
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&iface, sizeof(iface)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(IP_MULTICAST_IF) failed for %s: %d\n", interface_ip, WSAGetLastError());
        return -1;
    }

    printf("[UDP] Multicast egress interface set to %s\n", interface_ip);
    return 0;
}

int udp_enable_broadcast(udp_socket_t *socket)
{
    if (!socket)
        return -1;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return -1;

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) == SOCKET_ERROR)
    {
        printf("[UDP] setsockopt(SO_BROADCAST) failed: %d\n", WSAGetLastError());
        return -1;
    }

    printf("[UDP] Broadcast enabled\n");

    return 0;
}

uint16_t udp_get_port(udp_socket_t *socket)
{
    if (!socket)
        return 0;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock == INVALID_SOCKET)
        return 0;

    struct sockaddr_in addr = {0};
    int addr_len = sizeof(addr);

    if (getsockname(sock, (struct sockaddr *)&addr, &addr_len) == SOCKET_ERROR)
    {
        printf("[UDP] getsockname() failed: %d\n", WSAGetLastError());
        return 0;
    }

    return ntohs(addr.sin_port);
}

void udp_close(udp_socket_t *socket)
{
    if (!socket)
        return;

    SOCKET sock = (SOCKET)socket->socket;
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        socket->socket = (uint64_t)INVALID_SOCKET;
    }

    socket->port = 0;
}
