#ifndef AUDIO_OUTPUT_CORE_H
#define AUDIO_OUTPUT_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "spsc_ring.h"

typedef struct
{
    uint32_t in_frames_per_s;
    uint32_t out_frames_per_s;
    uint32_t ring_fill_ms;
} audio_output_stats_t;

typedef struct
{
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;

    spsc_ring_t ring;

    int16_t *mix_chunk;
    size_t chunk_frames;
    size_t chunk_samples;
    size_t preroll_samples;
    size_t max_latency_samples;
    int started;

    uint64_t in_samples_total;
    uint64_t out_samples_total;
    uint64_t in_samples_last;
    uint64_t out_samples_last;
    uint32_t stats_last_tick;

    float volume_db;
    float volume_linear;
} audio_output_core_t;

int audio_output_core_init(audio_output_core_t *core,
                           uint32_t sample_rate,
                           uint8_t channels,
                           uint8_t bits_per_sample,
                           uint32_t ring_seconds,
                           int16_t *ring_buffer,
                           size_t ring_buffer_samples,
                           size_t chunk_frames,
                           int16_t *mix_buffer,
                           size_t mix_buffer_samples,
                           uint32_t preroll_ms,
                           uint32_t max_latency_ms);

void audio_output_core_deinit(audio_output_core_t *core);
int audio_output_core_set_volume_db(audio_output_core_t *core, float volume_db);
int audio_output_core_write(audio_output_core_t *core, const int16_t *samples, size_t sample_count);
int audio_output_core_pop_chunk(audio_output_core_t *core);
void audio_output_core_on_chunk_played(audio_output_core_t *core);
int audio_output_core_collect_stats(audio_output_core_t *core, uint32_t now_ms, audio_output_stats_t *stats);

#endif // AUDIO_OUTPUT_CORE_H
