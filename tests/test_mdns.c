#include "protocol/mdns.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR("test", "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint8_t sent[1500];
static size_t sent_length;
static net_addr_t destination;
static int sends;
int net_udp_send(net_socket_t *s, const void *data, size_t n, const net_addr_t *peer)
{
    (void)s;
    CHECK(n <= sizeof(sent));
    memcpy(sent, data, n);
    sent_length = n;
    destination = *peer;
    ++sends;
    return (int)n;
}
static unsigned u16(const uint8_t *p)
{
    return (unsigned)p[0] * 256 + p[1];
}
static size_t skip_name(size_t p)
{
    while (p < sent_length && sent[p])
    {
        CHECK(sent[p] <= 63);
        p += 1 + sent[p];
    }
    CHECK(p < sent_length);
    return p + 1;
}
static void check_records(unsigned ttl)
{
    CHECK(u16(sent + 2) == 0x8400 && u16(sent + 6) == 4);
    size_t p = 12;
    unsigned types[] = {12, 33, 1, 16};
    for (unsigned i = 0; i < 4; ++i)
    {
        p = skip_name(p);
        CHECK(p + 10 <= sent_length);
        CHECK(u16(sent + p) == types[i]);
        CHECK(sent[p + 4] == 0 && sent[p + 5] == 0 && u16(sent + p + 6) == ttl);
        unsigned length = u16(sent + p + 8);
        p += 10;
        CHECK(p + length <= sent_length);
        if (types[i] == 1)
            CHECK(length == 4 && memcmp(sent + p, (uint8_t[]){192, 168, 1, 9}, 4) == 0);
        if (types[i] == 33)
            CHECK(u16(sent + p + 4) == 5000);
        if (types[i] == 16)
            CHECK(length == 10 && sent[p] == 9 && memcmp(sent + p + 1, "txtvers=1", 9) == 0);
        p += length;
    }
    CHECK(p == sent_length);
}
int main(void)
{
    const char *txt[] = {"txtvers=1"};
    mdns_config_t config = {"_raop._tcp.local",
                            "001122334455@TestSpeaker",
                            "TestSpeaker.local",
                            5000,
                            "192.168.1.9",
                            txt,
                            1};
    net_socket_t socket = NET_SOCKET_INIT;
    mdns_instance_t mdns;
    CHECK(mdns_create(&mdns, &config, &socket) == MDNS_OK);
    CHECK(mdns_announce(&mdns) == MDNS_OK);
    CHECK(strcmp(destination.ip, MDNS_MCAST_ADDR) == 0 && destination.port == 5353);
    check_records(60);
    CHECK(mdns_goodbye(&mdns) == MDNS_OK);
    check_records(0);
    uint8_t query[] = {0,   0,   0,   0,   0,   1,   0, 0,   0,    0,   0,   0,
                       5,   '_', 'r', 'a', 'o', 'p', 4, '_', 't',  'c', 'p', 5,
                       'l', 'o', 'c', 'a', 'l', 0,   0, 12,  0x80, 1};
    net_addr_t source = {"192.168.1.20", 54321};
    CHECK(mdns_handle_packet_from(&mdns, query, sizeof(query), &source) == MDNS_OK);
    CHECK(strcmp(destination.ip, source.ip) == 0 && destination.port == source.port);
    check_records(4500);
    query[sizeof(query) - 2] = 0;
    CHECK(mdns_handle_packet_from(&mdns, query, sizeof(query), &source) == MDNS_OK);
    CHECK(strcmp(destination.ip, MDNS_MCAST_ADDR) == 0);
    int previous = sends;
    query[2] = 0x80;
    CHECK(mdns_handle_packet_from(&mdns, query, sizeof(query), &source) == MDNS_OK &&
          sends == previous);
    uint8_t loop[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xc0, 12, 0, 12, 0, 1};
    CHECK(mdns_handle_packet(&mdns, loop, sizeof(loop)) == MDNS_ERR_INVALID_DATA);
    CHECK(sends == previous);
    LOG_INFO("test", "mDNS wire checks passed\n");
    return 0;
}
