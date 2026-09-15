#include "audio/buffered_audio.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR( "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint8_t incoming[1024];
static size_t available, position, chunk = 1;
static unsigned accepted, closed;
static uint32_t last_timestamp;
int net_tcp_listen(net_socket_t *s, const char *ip, uint16_t port)
{
    (void)ip;
    s->handle = 1;
    s->port = port;
    return 0;
}
int net_tcp_accept(net_socket_t *s, net_socket_t *c, net_addr_t *p, int timeout)
{
    (void)s;
    (void)timeout;
    c->handle = 2;
    strcpy(p->ip, "192.0.2.1");
    return 0;
}
int net_tcp_recv(net_socket_t *s, void *data, size_t size, int timeout)
{
    (void)s;
    (void)timeout;
    if (position == available)
        return NET_TIMEOUT;
    if (size > chunk)
        size = chunk;
    if (size > available - position)
        size = available - position;
    memcpy(data, incoming + position, size);
    position += size;
    return (int)size;
}
void net_close(net_socket_t *s)
{
    s->handle = UINTPTR_MAX;
    ++closed;
}
/**
 * @brief receive.
 * @param p Parameter named p.
 * @param context Parameter named context.
 */
static void receive(const rtp_packet_t *p, void *context)
{
    (void)context;
    CHECK(p->payload_len == 24);
    ++accepted;
    last_timestamp = p->header.timestamp;
}
/**
 * @brief packet.
 * @param sequence Parameter named sequence.
 * @param timestamp Parameter named timestamp.
 */
static void packet(uint32_t sequence, uint32_t timestamp)
{
    CHECK(available + 38 <= sizeof(incoming));
    uint8_t *p = incoming + available;
    memset(p, 0, 38);
    p[1] = 38;
    p[3] = (uint8_t)(sequence >> 16);
    p[4] = (uint8_t)(sequence >> 8);
    p[5] = (uint8_t)sequence;
    p[6] = (uint8_t)(timestamp >> 24);
    p[7] = (uint8_t)(timestamp >> 16);
    p[8] = (uint8_t)(timestamp >> 8);
    p[9] = (uint8_t)timestamp;
    available += 38;
}
/**
 * @brief drain.
 * @param b Parameter named b.
 */
static void drain(buffered_audio_t *b)
{
    while (position < available)
        CHECK(buffered_audio_poll(b, "192.0.2.1", receive, NULL) >= 0);
    CHECK(b->used == 0);
}
int main(void)
{
    buffered_audio_t b;
    buffered_audio_init(&b);
    CHECK(!buffered_audio_open(&b, 6000));
    packet(0x12fffe, 3949349609U);
    CHECK(!buffered_audio_poll(&b, "192.0.2.1", receive, NULL));
    CHECK(b.used == 1);
    buffered_audio_flush(&b, 0x12ffff);
    CHECK(b.used == 1 && !closed);
    packet(0x12ffff, 3949350633U);
    packet(0x130000, 3022307202U); /* New timeline is numerically earlier. */
    drain(&b);
    CHECK(accepted == 1 && last_timestamp == 3022307202U && !b.discarding);
    available = position = 0;
    chunk = 1024;
    buffered_audio_flush(&b, 0xffffff);
    packet(0xfffffe, 0xfffffc00);
    packet(0xffffff, 0);
    packet(0, 1024);
    drain(&b);
    CHECK(accepted == 2 && last_timestamp == 1024);
    available = position = 0;
    packet(1, 2048);
    packet(2, 3072);
    CHECK(buffered_audio_poll(&b, "192.0.2.1", receive, NULL) == 1);
    CHECK(position == 38 && accepted == 3);
    CHECK(buffered_audio_poll(&b, "192.0.2.1", receive, NULL) == 1);
    CHECK(position == 76 && accepted == 4);
    buffered_audio_flush(&b, 123);
    buffered_audio_disconnect(&b);
    CHECK(!b.discarding && b.used == 0);
    buffered_audio_close(&b);
    LOG_INFO( "Buffered seek, partial TCP records and 24-bit sequence wrap passed\n");
    return 0;
}
