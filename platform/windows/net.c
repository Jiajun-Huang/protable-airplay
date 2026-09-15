#include "net.h"

#include <limits.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/**
 * @brief now_ms.
 * @return Function result.
 */
static uint32_t now_ms(void)
{
    return (uint32_t)GetTickCount64();
}
/**
 * @brief set_nonblocking.
 * @param fd Parameter named fd.
 * @return Function result.
 */
static int set_nonblocking(SOCKET fd)
{
    u_long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled);
}

/**
 * @brief would_block.
 * @param error Parameter named error.
 * @return Function result.
 */
static int would_block(int error)
{
    return error == WSAEWOULDBLOCK;
}

/**
 * @brief remaining_ms.
 * @param start Parameter named start.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
static int remaining_ms(uint32_t start, int timeout_ms)
{
    uint32_t elapsed;
    if (timeout_ms < 0)
        return -1;
    elapsed = now_ms() - start;
    return elapsed >= (uint32_t)timeout_ms ? 0 : timeout_ms - (int)elapsed;
}

/**
 * @brief socket_valid.
 * @param sock Parameter named sock.
 * @return Function result.
 */
static int socket_valid(const net_socket_t *sock)
{
    return sock && sock->handle != UINTPTR_MAX;
}

/**
 * @brief reset_socket.
 * @param sock Parameter named sock.
 */
static void reset_socket(net_socket_t *sock)
{
    if (sock)
    {
        sock->handle = UINTPTR_MAX;
        sock->port = 0;
    }
}

/**
 * @brief make_address.
 * @param addr Parameter named addr.
 * @param ip Parameter named ip.
 * @param port Parameter named port.
 * @return Function result.
 */
static int make_address(struct sockaddr_in *addr, const char *ip, uint16_t port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    if (!ip || !ip[0])
    {
        addr->sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }
    return InetPtonA(AF_INET, ip, &addr->sin_addr) == 1 ? 0 : NET_ERROR;
}

/**
 * @brief copy_peer.
 * @param peer Parameter named peer.
 * @param addr Parameter named addr.
 * @return Function result.
 */
static int copy_peer(net_addr_t *peer, const struct sockaddr_in *addr)
{
    if (!peer)
        return 0;
    if (!InetNtopA(AF_INET, (void *)&addr->sin_addr, peer->ip, sizeof(peer->ip)))
        return NET_ERROR;
    peer->port = ntohs(addr->sin_port);
    return 0;
}

int net_init(void)
{
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : NET_ERROR;
}

void net_deinit(void)
{
    WSACleanup();
}

void net_close(net_socket_t *sock)
{
    if (socket_valid(sock))
        closesocket((SOCKET)sock->handle);
    reset_socket(sock);
}

/**
 * @brief open_bound.
 * @param sock Parameter named sock.
 * @param ip Parameter named ip.
 * @param port Parameter named port.
 * @param type Parameter named type.
 * @return Function result.
 */
static int open_bound(net_socket_t *sock, const char *ip, uint16_t port, int type)
{
    SOCKET fd;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    int reuse = 1;

    if (!sock)
        return NET_ERROR;
    reset_socket(sock);
    if (make_address(&addr, ip, port) != 0)
        return NET_ERROR;
    fd = socket(AF_INET, type, 0);
    if (fd == INVALID_SOCKET)
        return NET_ERROR;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) != 0 ||
        set_nonblocking(fd) != 0)
        goto fail;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        (type == SOCK_STREAM && listen(fd, 4) != 0) ||
        getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0)
        goto fail;
    sock->handle = (uintptr_t)fd;
    sock->port = ntohs(addr.sin_port);
    return 0;
fail:
    closesocket(fd);
    return NET_ERROR;
}

int net_tcp_listen(net_socket_t *sock, const char *ip, uint16_t port)
{
    return open_bound(sock, ip, port, SOCK_STREAM);
}

int net_udp_bind(net_socket_t *sock, uint16_t port)
{
    return open_bound(sock, NULL, port, SOCK_DGRAM);
}

/**
 * @brief wait_sockets.
 * @param sockets Parameter named sockets.
 * @param count Parameter named count.
 * @param ready Parameter named ready.
 * @param timeout_ms Parameter named timeout_ms.
 * @param writing Parameter named writing.
 * @return Function result.
 */
static int wait_sockets(
    const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms, int writing)
{
    uint32_t start = now_ms();
    size_t i;
    if (!sockets || !ready || count == 0 || count > FD_SETSIZE || timeout_ms < -1)
        return NET_ERROR;
    memset(ready, 0, count);
    for (;;)
    {
        fd_set fds;
        struct timeval tv;
        struct timeval *tv_ptr = NULL;
        int result, active = 0;
        FD_ZERO(&fds);
        for (i = 0; i < count; i++)
        {
            SOCKET fd;
            if (!socket_valid(&sockets[i]))
                continue;
            fd = (SOCKET)sockets[i].handle;
            FD_SET(fd, &fds);
            active++;
        }
        if (!active)
            return NET_ERROR;
        if (timeout_ms >= 0)
        {
            int left = remaining_ms(start, timeout_ms);
            tv.tv_sec = left / 1000;
            tv.tv_usec = (left % 1000) * 1000;
            tv_ptr = &tv;
        }
        result = select(0, writing ? NULL : &fds, writing ? &fds : NULL, NULL, tv_ptr);
        if (result == 0)
            return 0;
        if (result > 0)
        {
            int total = 0;
            for (i = 0; i < count; i++)
            {
                if (socket_valid(&sockets[i]) && FD_ISSET((SOCKET)sockets[i].handle, &fds))
                {
                    ready[i] = 1;
                    total++;
                }
            }
            return total;
        }
        if (WSAGetLastError() != WSAEINTR)
            return NET_ERROR;
        if (timeout_ms >= 0 && remaining_ms(start, timeout_ms) == 0)
            return 0;
    }
}

int net_wait(const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms)
{
    return wait_sockets(sockets, count, ready, timeout_ms, 0);
}

/**
 * @brief wait_one.
 * @param sock Parameter named sock.
 * @param timeout_ms Parameter named timeout_ms.
 * @param writing Parameter named writing.
 * @return Function result.
 */
static int wait_one(net_socket_t *sock, int timeout_ms, int writing)
{
    uint8_t ready;
    int result = wait_sockets(sock, 1, &ready, timeout_ms, writing);
    return result > 0 ? 0 : result == 0 ? NET_TIMEOUT : NET_ERROR;
}

int net_tcp_accept(net_socket_t *listener, net_socket_t *client, net_addr_t *peer, int timeout_ms)
{
    struct sockaddr_in addr;
    int length = sizeof(addr);
    SOCKET fd;
    int result;
    if (!socket_valid(listener) || !client || listener == client)
        return NET_ERROR;
    reset_socket(client);
    result = wait_one(listener, timeout_ms, 0);
    if (result != 0)
        return result;
    fd = accept((SOCKET)listener->handle, (struct sockaddr *)&addr, &length);
    if (fd == INVALID_SOCKET)
        return would_block(WSAGetLastError()) ? NET_TIMEOUT : NET_ERROR;
    if (set_nonblocking(fd) != 0 || copy_peer(peer, &addr) != 0)
    {
        closesocket(fd);
        return NET_ERROR;
    }
    client->handle = (uintptr_t)fd;
    client->port = listener->port;
    return 0;
}

int net_tcp_recv(net_socket_t *sock, void *data, size_t capacity, int timeout_ms)
{
    int result;
    if (!socket_valid(sock) || !data || capacity == 0 || capacity > INT_MAX)
        return NET_ERROR;
    result = wait_one(sock, timeout_ms, 0);
    if (result != 0)
        return result;
    result = (int)recv((SOCKET)sock->handle, (char *)data, (int)capacity, 0);
    if (result < 0)
        return would_block(WSAGetLastError()) ? NET_TIMEOUT : NET_ERROR;
    return result;
}

int net_tcp_send_all(net_socket_t *sock, const void *data, size_t length, int timeout_ms)
{
    const char *bytes = (const char *)data;
    uint32_t start = now_ms();
    size_t sent = 0;
    if (!socket_valid(sock) || (!data && length) || timeout_ms < -1)
        return NET_ERROR;
    while (sent < length)
    {
        size_t chunk = length - sent;
        int result = wait_one(sock, remaining_ms(start, timeout_ms), 1);
        if (result != 0)
            return result;
        if (chunk > INT_MAX)
            chunk = INT_MAX;
        result = (int)send((SOCKET)sock->handle, bytes + sent, (int)chunk, 0);
        if (result > 0)
            sent += (size_t)result;
        else if (result == 0)
            return NET_ERROR;
        else
        {
            int error = WSAGetLastError();
            if (!would_block(error) && error != WSAEINTR)
                return NET_ERROR;
        }
        if (sent < length && timeout_ms >= 0 && remaining_ms(start, timeout_ms) == 0)
            return NET_TIMEOUT;
    }
    return 0;
}

int net_udp_recv(net_socket_t *sock, void *data, size_t capacity, net_addr_t *peer, int timeout_ms)
{
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    int result;
    if (!socket_valid(sock) || !data || capacity == 0 || capacity > INT_MAX)
        return NET_ERROR;
    result = wait_one(sock, timeout_ms, 0);
    if (result != 0)
        return result;
    result = (int)recvfrom(
        (SOCKET)sock->handle, (char *)data, (int)capacity, 0, (struct sockaddr *)&addr, &addr_len);
    if (result < 0 && WSAGetLastError() == WSAEMSGSIZE)
        return NET_TIMEOUT;
    if (result < 0)
        return would_block(WSAGetLastError()) ? NET_TIMEOUT : NET_ERROR;
    if (copy_peer(peer, &addr) != 0)
        return NET_ERROR;
    return result;
}

int net_udp_send(net_socket_t *sock, const void *data, size_t length, const net_addr_t *peer)
{
    struct sockaddr_in addr;
    int result;
    if (!socket_valid(sock) || (!data && length) || length > INT_MAX || !peer || peer->port == 0 ||
        make_address(&addr, peer->ip, peer->port) != 0)
        return NET_ERROR;
    result = (int)sendto((SOCKET)sock->handle,
                         data ? (const char *)data : "",
                         (int)length,
                         0,
                         (struct sockaddr *)&addr,
                         sizeof(addr));
    if (result < 0)
        return would_block(WSAGetLastError()) ? NET_TIMEOUT : NET_ERROR;
    return result;
}

int net_udp_join(net_socket_t *sock, const char *group, const char *interface_ip)
{
    struct sockaddr_in group_addr, interface_addr;
    struct ip_mreq membership;
    SOCKET fd;
    unsigned int ttl = 255;
    if (!socket_valid(sock) || !group || make_address(&group_addr, group, 0) != 0 ||
        make_address(&interface_addr, interface_ip, 0) != 0 ||
        (ntohl(group_addr.sin_addr.s_addr) & 0xf0000000UL) != 0xe0000000UL)
        return NET_ERROR;
    fd = (SOCKET)sock->handle;
    membership.imr_multiaddr = group_addr.sin_addr;
    membership.imr_interface = interface_addr.sin_addr;
    if (setsockopt(fd,
                   IPPROTO_IP,
                   IP_MULTICAST_IF,
                   (const char *)&interface_addr.sin_addr,
                   sizeof(interface_addr.sin_addr)) != 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) != 0 ||
        setsockopt(
            fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&membership, sizeof(membership)) != 0)
        return NET_ERROR;
    return 0;
}
