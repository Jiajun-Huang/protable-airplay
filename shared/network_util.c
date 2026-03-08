#include "network_util.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static int winsock_initialized = 0;

static int ensure_winsock_init(void)
{
    if (winsock_initialized)
        return 0;

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        return -1;

    winsock_initialized = 1;
    return 0;
}
#else
#define ensure_winsock_init() 0
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define INVALID_SOCKET -1
#define SOCKET int
#define closesocket close
#endif

int net_str_to_ipv4(const char *ip_str, uint32_t *out_addr)
{
    if (!ip_str || !out_addr)
        return -1;

    struct in_addr addr;

#ifdef _WIN32
    // Windows: use InetPtonA
    if (InetPtonA(AF_INET, ip_str, &addr) != 1)
    {
        // Fallback to inet_addr for compatibility
        addr.s_addr = inet_addr(ip_str);
        if (addr.s_addr == INADDR_NONE && strcmp(ip_str, "255.255.255.255") != 0)
            return -1;
    }
#else
    // POSIX: use inet_pton
    if (inet_pton(AF_INET, ip_str, &addr) != 1)
        return -1;
#endif

    *out_addr = addr.s_addr;
    return 0;
}

int net_ipv4_to_str(uint32_t addr, char *out_str, size_t str_len)
{
    if (!out_str || str_len < 16)
        return -1;

    struct in_addr in_addr;
    in_addr.s_addr = addr;

#ifdef _WIN32
    // Windows: use InetNtopA
    if (InetNtopA(AF_INET, &in_addr, out_str, (DWORD)str_len) == NULL)
    {
        // Fallback to inet_ntoa (not thread-safe but works)
        const char *str = inet_ntoa(in_addr);
        if (str)
        {
            strncpy(out_str, str, str_len - 1);
            out_str[str_len - 1] = '\0';
            return 0;
        }
        return -1;
    }
#else
    // POSIX: use inet_ntop
    if (inet_ntop(AF_INET, &in_addr, out_str, str_len) == NULL)
        return -1;
#endif

    return 0;
}

int net_get_local_ipv4(char *out_str, size_t str_len)
{
    SOCKET s;
    struct sockaddr_in dst;
    struct sockaddr_in local;
    int local_len = (int)sizeof(local);
    uint32_t dns_ip;

    if (!out_str || str_len < 16)
        return -1;

    if (ensure_winsock_init() != 0)
        return -1;

    out_str[0] = '\0';

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return -1;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    // Use Google DNS as remote endpoint (no packets actually sent for UDP)
    if (net_str_to_ipv4("8.8.8.8", &dns_ip) != 0)
    {
        closesocket(s);
        return -1;
    }
    dst.sin_addr.s_addr = dns_ip;

    // Connect to determine outbound interface (doesn't send packets for UDP)
    if (connect(s, (const struct sockaddr *)&dst, sizeof(dst)) != 0)
    {
        closesocket(s);
        return -1;
    }

    memset(&local, 0, sizeof(local));
    if (getsockname(s, (struct sockaddr *)&local, &local_len) != 0)
    {
        closesocket(s);
        return -1;
    }

    closesocket(s);

    return net_ipv4_to_str(local.sin_addr.s_addr, out_str, str_len);
}
