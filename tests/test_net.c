#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "net.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET test_socket_t;
#define test_close   closesocket
#define TEST_SHUT_WR SD_SEND
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int test_socket_t;
#define test_close   close
#define TEST_SHUT_WR SHUT_WR
#endif

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR( "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/**
 * @brief test_now_ms.
 * @return Function result.
 */
static uint64_t test_now_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec time;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
#endif
}

int main(void)
{
    CHECK(net_init() == 0);
    net_socket_t a = NET_SOCKET_INIT, b = NET_SOCKET_INIT;
    net_socket_t listener = NET_SOCKET_INIT, client = NET_SOCKET_INIT;
    net_addr_t peer, destination = {"127.0.0.1", 0};
    char buffer[64];
    CHECK(net_udp_bind(&a, 0) == 0 && a.port != 0);
    CHECK(net_udp_bind(&b, 0) == 0 && b.port != a.port);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), &peer, 10) == NET_TIMEOUT);
    destination.port = b.port;
    CHECK(net_udp_send(&a, "hello", 5, &destination) == 5);
    net_socket_t sockets[] = {a, b};
    uint8_t ready[2];
    CHECK(net_wait(sockets, 2, ready, 1000) == 1 && ready[1] && !ready[0]);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), &peer, 0) == 5);
    CHECK(memcmp(buffer, "hello", 5) == 0 && peer.port == a.port);
    CHECK(net_udp_send(&a, "", 0, &destination) == 0);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), NULL, 1000) == 0);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), NULL, 0) == NET_TIMEOUT);
    char oversized[128];
    memset(oversized, 'x', sizeof(oversized));
    CHECK(net_udp_send(&a, oversized, sizeof(buffer), &destination) == sizeof(buffer));
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), NULL, 1000) == sizeof(buffer));
    CHECK(net_udp_send(&a, oversized, sizeof(oversized), &destination) == sizeof(oversized));
    CHECK(net_udp_send(&a, "after", 5, &destination) == 5);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), NULL, 1000) == NET_TIMEOUT);
    CHECK(net_udp_recv(&b, buffer, sizeof(buffer), NULL, 1000) == 5);
    CHECK(memcmp(buffer, "after", 5) == 0);
    net_close(&a);
    net_close(&a);
    net_close(&b);
    CHECK(a.handle == UINTPTR_MAX &&
          net_udp_recv(&a, buffer, sizeof(buffer), NULL, 0) == NET_ERROR);

    CHECK(net_tcp_listen(&listener, "127.0.0.1", 0) == 0);
    CHECK(net_tcp_accept(&listener, &client, &peer, 10) == NET_TIMEOUT);
    test_socket_t sender = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(sender != (test_socket_t)-1);
    int small = 4096;
    CHECK(setsockopt(sender, SOL_SOCKET, SO_RCVBUF, (const char *)&small, sizeof(small)) == 0);
    struct sockaddr_in endpoint = {0};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    endpoint.sin_port = htons(listener.port);
    CHECK(connect(sender, (struct sockaddr *)&endpoint, sizeof(endpoint)) == 0);
    CHECK(net_tcp_accept(&listener, &client, &peer, 1000) == 0);
    CHECK(strcmp(peer.ip, "127.0.0.1") == 0);
    CHECK(net_tcp_recv(&client, buffer, sizeof(buffer), 0) == NET_TIMEOUT);
    CHECK(send(sender, "request", 7, 0) == 7);
    CHECK(net_tcp_recv(&client, buffer, sizeof(buffer), -1) == 7);
    CHECK(net_tcp_send_all(&client, "response", 8, 1000) == 0);
    CHECK(recv(sender, buffer, sizeof(buffer), 0) == 8 && memcmp(buffer, "response", 8) == 0);
    /* Fill the local queue; one large write can succeed before backpressure. */
    CHECK(setsockopt((test_socket_t)client.handle,
                     SOL_SOCKET,
                     SO_SNDBUF,
                     (const char *)&small,
                     sizeof(small)) == 0);
    char *large = calloc(1, 64 * 1024);
    CHECK(large != NULL);
    int send_result = 0;
    unsigned attempts;
    uint64_t send_started = test_now_ms();
    for (attempts = 0; attempts < 1024 && send_result == 0; attempts++)
    {
        send_result = net_tcp_send_all(&client, large, 64 * 1024, 50);
        CHECK(test_now_ms() - send_started < 2000);
    }
    CHECK(send_result == NET_TIMEOUT);
    free(large);
    test_close(sender);
    net_close(&client);
    /* Verify orderly peer close separately from the deliberately incomplete send. */
    sender = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(connect(sender, (struct sockaddr *)&endpoint, sizeof(endpoint)) == 0);
    CHECK(net_tcp_accept(&listener, &client, &peer, 1000) == 0);
    test_close(sender);
    CHECK(net_tcp_recv(&client, buffer, sizeof(buffer), 1000) == 0);
    /* A closed write side must return an error without terminating on SIGPIPE. */
    CHECK(shutdown((test_socket_t)client.handle, TEST_SHUT_WR) == 0);
    CHECK(net_tcp_send_all(&client, "x", 1, 1000) == NET_ERROR);
    net_close(&client);
    net_close(&listener);
    net_deinit();
    LOG_INFO( "Network contract checks passed\n");
    return 0;
}
