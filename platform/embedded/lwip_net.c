#include "../../include/platform_if.h"
#include <stdint.h>
#include <stddef.h>

// TODO: implement using lwIP sockets

void lwip_tcp_send(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

void lwip_udp_send(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

const raop_net_if_t lwip_net_if = {
    .tcp_send = lwip_tcp_send,
    .udp_send = lwip_udp_send,
};
