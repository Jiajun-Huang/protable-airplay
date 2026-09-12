#include "buffered_audio.h"
#include <string.h>
void buffered_audio_init(buffered_audio_t *b)
{
    memset(b, 0, sizeof(*b));
    b->listener = b->client = (net_socket_t)NET_SOCKET_INIT;
}
int buffered_audio_open(buffered_audio_t *b, uint16_t port)
{
    return net_tcp_listen(&b->listener, NULL, port);
}
void buffered_audio_disconnect(buffered_audio_t *b)
{
    net_close(&b->client); b->used = 0;
}
void buffered_audio_close(buffered_audio_t *b) { buffered_audio_disconnect(b); net_close(&b->listener); }
int buffered_audio_poll(buffered_audio_t *b, const char *ip, rtp_audio_callback callback, void *context)
{
    if (b->client.handle == UINTPTR_MAX) {
        net_addr_t peer;
        int result = net_tcp_accept(&b->listener, &b->client, &peer, 0);
        if (result == NET_TIMEOUT) return 0;
        if (result != 0) return -1;
        if (!ip[0] || strcmp(peer.ip, ip)) { buffered_audio_disconnect(b); return 0; }
    }
    if (b->used < 2) {
        int n = net_tcp_recv(&b->client, b->data + b->used, 2 - b->used, 0);
        if (n == NET_TIMEOUT) return 0;
        if (n <= 0) { buffered_audio_disconnect(b); return 0; }
        b->used += (size_t)n;
        return 0;
    }
    size_t need = (size_t)b->data[0] << 8 | b->data[1];
    if (need < 38 || need > sizeof(b->data)) { buffered_audio_disconnect(b); return 0; }
    int n = net_tcp_recv(&b->client, b->data + b->used, need - b->used, 0);
    if (n == NET_TIMEOUT) return 0;
    if (n <= 0) { buffered_audio_disconnect(b); return 0; }
    b->used += (size_t)n;
    if (b->used < need) return 0;
    const uint8_t *p = b->data + 2;
    uint32_t sequence = (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    rtp_packet_t packet = {0};
    packet.header.version = 2; packet.header.payload_type = 96;
    /* Buffered audio uses a 24-bit sequence; the reorder queue tracks its low 16 bits. */
    packet.header.sequence = (uint16_t)sequence;
    packet.header.timestamp = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
    packet.header.ssrc = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    packet.payload = p + 12; packet.payload_len = need - 14;
    callback(&packet, context);
    b->used = 0;
    return 1;
}
