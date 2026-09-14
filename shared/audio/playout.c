#include "audio/playout.h"
#include <string.h>

void playout_reset(playout_t *queue)
{
    for (unsigned i = 0; i < AIRPLAY_PLAYOUT_PACKETS; ++i)
        queue->packets[i].valid = 0;
    queue->count = 0;
    queue->next_sequence = 0;
    queue->started = queue->have_sequence = queue->have_floor = 0;
}
void playout_set_floor(playout_t *queue, uint32_t timestamp, int exclusive)
{
    playout_reset(queue);
    queue->timestamp_floor = timestamp;
    queue->floor_exclusive = exclusive;
    queue->have_floor = 1;
}
int playout_push(playout_t *queue, const rtp_packet_t *packet)
{
    if (!packet->payload_len || packet->payload_len > PLAYOUT_PAYLOAD_BYTES)
        return -1;
    if (queue->have_floor)
    {
        int32_t delta = (int32_t)(packet->header.timestamp - queue->timestamp_floor);
        if (delta < 0 || (delta == 0 && queue->floor_exclusive))
            return 0;
    }
    int16_t distance = (int16_t)(packet->header.sequence - queue->next_sequence);
    if (queue->have_sequence && queue->started && distance < 0)
        return 0;
    playout_packet_t *slot = NULL;
    for (unsigned i = 0; i < AIRPLAY_PLAYOUT_PACKETS; ++i)
    {
        if (queue->packets[i].valid)
        {
            if (queue->packets[i].header.sequence == packet->header.sequence)
                return 0;
        }
        else if (!slot)
            slot = &queue->packets[i];
    }
    if (!slot)
        return -1;
    if (!queue->have_sequence || (!queue->started && distance < 0))
        queue->next_sequence = packet->header.sequence;
    queue->have_sequence = 1;
    slot->header = packet->header;
    slot->length = packet->payload_len;
    memcpy(slot->payload, packet->payload, slot->length);
    slot->valid = 1;
    ++queue->count;
    return 1;
}
playout_packet_t *playout_peek(playout_t *queue)
{
    playout_packet_t *first = NULL;
    for (unsigned i = 0; i < AIRPLAY_PLAYOUT_PACKETS; ++i)
    {
        playout_packet_t *slot = &queue->packets[i];
        if (slot->valid &&
            (!first || (uint16_t)(slot->header.sequence - queue->next_sequence) <
                           (uint16_t)(first->header.sequence - queue->next_sequence)))
            first = slot;
    }
    return first;
}
void playout_pop(playout_t *queue, playout_packet_t *packet)
{
    queue->next_sequence = (uint16_t)(packet->header.sequence + 1);
    queue->started = 1;
    queue->have_floor = 0;
    packet->valid = 0;
    --queue->count;
}
