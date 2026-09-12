#ifndef AIRPLAY_BUFFERED_AUDIO_H
#define AIRPLAY_BUFFERED_AUDIO_H
#include "rtp.h"
typedef struct {
    net_socket_t listener, client;
    uint8_t data[RTP_BUFFER_SIZE + 2];
    size_t used;
    uint32_t discard_sequence;
    int discarding;
} buffered_audio_t;
void buffered_audio_init(buffered_audio_t *b);
int buffered_audio_open(buffered_audio_t *b, uint16_t port);
void buffered_audio_disconnect(buffered_audio_t *b);
void buffered_audio_close(buffered_audio_t *b);
/* Discard through this 24-bit wire sequence, preserving any partial TCP record. */
void buffered_audio_flush(buffered_audio_t *b, uint32_t until_sequence);
/* One complete packet per poll; partial TCP records survive timeouts. */
int buffered_audio_poll(buffered_audio_t *b, const char *peer_ip,
                         rtp_audio_callback callback, void *context);
#endif
