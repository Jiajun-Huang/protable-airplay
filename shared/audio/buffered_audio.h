#ifndef AIRPLAY_BUFFERED_AUDIO_H
#define AIRPLAY_BUFFERED_AUDIO_H
#include "protocol/rtp.h"

/* AirPlay 2 type 103 transport. It accepts length-prefixed RTP records over TCP
 * and can discard stale records through a 24-bit seek sequence boundary. */

/* Caller-owned listener, accepted connection, and partial-record buffer. */
typedef struct
{
    net_socket_t listener, client;
    uint8_t data[RTP_BUFFER_SIZE + 2];
    size_t used;
    uint32_t discard_sequence;
    int discarding;
} buffered_audio_t;

/* Initialize an unopened buffered transport instance. */
void buffered_audio_init(buffered_audio_t *b);
/* Open the TCP listener on port. */
int buffered_audio_open(buffered_audio_t *b, uint16_t port);
/* Close the active sender connection and clear partial receive state. */
void buffered_audio_disconnect(buffered_audio_t *b);
/* Close the sender and listener sockets. */
void buffered_audio_close(buffered_audio_t *b);
/* Discard through this 24-bit wire sequence, preserving any partial TCP record. */
void buffered_audio_flush(buffered_audio_t *b, uint32_t until_sequence);
/* Accept or receive at most one complete record and deliver it through callback.
 * Partial TCP records survive timeouts. Returns 1 for a consumed record, 0 on idle. */
int buffered_audio_poll(buffered_audio_t *b,
                        const char *peer_ip,
                        rtp_audio_callback callback,
                        void *context);
#endif
