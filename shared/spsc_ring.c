#include "spsc_ring.h"

#include <string.h>

int spsc_ring_init(spsc_ring_t *ring, int16_t *buffer, size_t capacity_samples)
{
    if (!ring || !buffer || capacity_samples == 0)
        return -1;

    memset(ring, 0, sizeof(*ring));
    ring->buffer = buffer;
    ring->capacity = capacity_samples;
    return 0;
}

void spsc_ring_deinit(spsc_ring_t *ring)
{
    if (!ring)
        return;

    memset(ring, 0, sizeof(*ring));
}

size_t spsc_ring_capacity(const spsc_ring_t *ring)
{
    return ring ? ring->capacity : 0;
}

size_t spsc_ring_fill(const spsc_ring_t *ring)
{
    return ring ? ring->fill : 0;
}

size_t spsc_ring_free(const spsc_ring_t *ring)
{
    if (!ring || ring->capacity < ring->fill)
        return 0;

    return ring->capacity - ring->fill;
}

size_t spsc_ring_push(spsc_ring_t *ring, const int16_t *src, size_t count)
{
    size_t first;
    size_t push_count;

    if (!ring || !ring->buffer || !src || count == 0)
        return 0;

    push_count = count;
    if (push_count > spsc_ring_free(ring))
        push_count = spsc_ring_free(ring);

    if (push_count == 0)
        return 0;

    first = push_count;
    if (ring->write_pos + first > ring->capacity)
        first = ring->capacity - ring->write_pos;

    memcpy(ring->buffer + ring->write_pos, src, first * sizeof(int16_t));
    if (push_count > first)
        memcpy(ring->buffer, src + first, (push_count - first) * sizeof(int16_t));

    ring->write_pos = (ring->write_pos + push_count) % ring->capacity;
    ring->fill += push_count;
    return push_count;
}

size_t spsc_ring_pop(spsc_ring_t *ring, int16_t *dst, size_t count)
{
    size_t first;
    size_t pop_count;

    if (!ring || !ring->buffer || !dst || count == 0)
        return 0;

    pop_count = count;
    if (pop_count > ring->fill)
        pop_count = ring->fill;

    if (pop_count == 0)
        return 0;

    first = pop_count;
    if (ring->read_pos + first > ring->capacity)
        first = ring->capacity - ring->read_pos;

    memcpy(dst, ring->buffer + ring->read_pos, first * sizeof(int16_t));
    if (pop_count > first)
        memcpy(dst + first, ring->buffer, (pop_count - first) * sizeof(int16_t));

    ring->read_pos = (ring->read_pos + pop_count) % ring->capacity;
    ring->fill -= pop_count;
    return pop_count;
}

void spsc_ring_drop_oldest(spsc_ring_t *ring, size_t count)
{
    size_t drop_count;

    if (!ring || !ring->buffer || count == 0)
        return;

    drop_count = count;
    if (drop_count > ring->fill)
        drop_count = ring->fill;

    ring->read_pos = (ring->read_pos + drop_count) % ring->capacity;
    ring->fill -= drop_count;
}
