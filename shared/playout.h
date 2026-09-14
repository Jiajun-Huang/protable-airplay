#ifndef AIRPLAY_PLAYOUT_H
#define AIRPLAY_PLAYOUT_H
#include "rtp.h"
#include "alac.h"

/* 16-bit stereo PCM plus ALAC framing overhead. */
#define PLAYOUT_PAYLOAD_BYTES (ALAC_MAX_SAMPLES_PER_FRAME * 4 + 64)
typedef struct
{
    rtp_header_t header;
    size_t length;
    uint8_t payload[PLAYOUT_PAYLOAD_BYTES];
    int valid;
} playout_packet_t;
typedef struct
{
    playout_packet_t packets[AIRPLAY_PLAYOUT_PACKETS];
    uint16_t next_sequence;
    unsigned count;
    int started, have_sequence;
    int have_floor, floor_exclusive;
    uint32_t timestamp_floor;
} playout_t;

void playout_reset(playout_t *queue);
void playout_set_floor(playout_t *queue, uint32_t timestamp, int exclusive);
/* 1 accepted, 0 duplicate/stale, -1 capacity or payload limit. */
int playout_push(playout_t *queue, const rtp_packet_t *packet);
playout_packet_t *playout_peek(playout_t *queue);
void playout_pop(playout_t *queue, playout_packet_t *packet);
#endif
