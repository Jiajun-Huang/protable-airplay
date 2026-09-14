#ifndef AIRPLAY_PLAYOUT_H
#define AIRPLAY_PLAYOUT_H
#include "codec/alac.h"
#include "protocol/rtp.h"

/* Bounded compressed-packet reorder queue shared by realtime and buffered audio. */

/* 16-bit stereo PCM plus ALAC framing overhead. */
#define PLAYOUT_PAYLOAD_BYTES (ALAC_MAX_SAMPLES_PER_FRAME * 4 + 64)
typedef struct
{
    rtp_header_t header;
    size_t length;
    uint8_t payload[PLAYOUT_PAYLOAD_BYTES];
    int valid;
} playout_packet_t;

/* Fixed-capacity queue ordered by RTP sequence and filtered by timestamp boundaries. */
typedef struct
{
    playout_packet_t packets[AIRPLAY_PLAYOUT_PACKETS];
    uint16_t next_sequence;
    unsigned count;
    int started, have_sequence;
    int have_floor, floor_exclusive;
    uint32_t timestamp_floor;
} playout_t;

/* Remove every queued packet and clear sequence and timestamp boundaries. */
void playout_reset(playout_t *queue);
/* Set the RTP timestamp accepted at the next RECORD or FLUSH boundary. */
void playout_set_floor(playout_t *queue, uint32_t timestamp, int exclusive);
/* 1 accepted, 0 duplicate/stale, -1 capacity or payload limit. */
int playout_push(playout_t *queue, const rtp_packet_t *packet);
/* Return the next ordered packet without removing it, or NULL when empty. */
playout_packet_t *playout_peek(playout_t *queue);
/* Remove a packet previously returned by playout_peek. */
void playout_pop(playout_t *queue, playout_packet_t *packet);
#endif
