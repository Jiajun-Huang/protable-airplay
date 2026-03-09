#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int16_t *buffer;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t fill;
} spsc_ring_t;

int spsc_ring_init(spsc_ring_t *ring, size_t capacity_samples);
void spsc_ring_deinit(spsc_ring_t *ring);

size_t spsc_ring_capacity(const spsc_ring_t *ring);
size_t spsc_ring_fill(const spsc_ring_t *ring);
size_t spsc_ring_free(const spsc_ring_t *ring);

size_t spsc_ring_push(spsc_ring_t *ring, const int16_t *src, size_t count);
size_t spsc_ring_pop(spsc_ring_t *ring, int16_t *dst, size_t count);
void spsc_ring_drop_oldest(spsc_ring_t *ring, size_t count);

#endif // SPSC_RING_H
