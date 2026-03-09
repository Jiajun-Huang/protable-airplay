#include "audio_output_core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int16_t clamp_i16(int x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

static void ring_pop_samples(audio_output_core_t *core, int16_t *dst, size_t count)
{
    size_t first = count;
    if (core->ring_read_pos + first > core->ring_capacity_samples)
        first = core->ring_capacity_samples - core->ring_read_pos;

    memcpy(dst, core->ring + core->ring_read_pos, first * sizeof(int16_t));

    if (count > first)
        memcpy(dst + first, core->ring, (count - first) * sizeof(int16_t));

    core->ring_read_pos = (core->ring_read_pos + count) % core->ring_capacity_samples;
    core->ring_fill_samples -= count;
}

static void ring_push_samples(audio_output_core_t *core, const int16_t *src, size_t count)
{
    size_t first = count;
    if (core->ring_write_pos + first > core->ring_capacity_samples)
        first = core->ring_capacity_samples - core->ring_write_pos;

    memcpy(core->ring + core->ring_write_pos, src, first * sizeof(int16_t));

    if (count > first)
        memcpy(core->ring, src + first, (count - first) * sizeof(int16_t));

    core->ring_write_pos = (core->ring_write_pos + count) % core->ring_capacity_samples;
    core->ring_fill_samples += count;
}

int audio_output_core_init(audio_output_core_t *core,
                           uint32_t sample_rate,
                           uint8_t channels,
                           uint8_t bits_per_sample,
                           uint32_t ring_seconds,
                           size_t chunk_frames,
                           uint32_t preroll_ms,
                           uint32_t max_latency_ms)
{
    if (!core || channels == 0 || sample_rate == 0 || chunk_frames == 0)
        return -1;

    memset(core, 0, sizeof(*core));

    core->sample_rate = sample_rate;
    core->channels = channels;
    core->bits_per_sample = bits_per_sample;
    core->chunk_frames = chunk_frames;
    core->chunk_samples = chunk_frames * channels;
    core->preroll_samples = ((size_t)sample_rate * channels * preroll_ms) / 1000;
    core->max_latency_samples = ((size_t)sample_rate * channels * max_latency_ms) / 1000;
    core->ring_capacity_samples = (size_t)sample_rate * channels * ring_seconds;

    core->ring = (int16_t *)malloc(core->ring_capacity_samples * sizeof(int16_t));
    core->mix_chunk = (int16_t *)malloc(core->chunk_samples * sizeof(int16_t));
    if (!core->ring || !core->mix_chunk)
    {
        audio_output_core_deinit(core);
        return -1;
    }

    core->volume_db = 0.0f;
    core->volume_linear = 1.0f;
    return 0;
}

void audio_output_core_deinit(audio_output_core_t *core)
{
    if (!core)
        return;

    free(core->mix_chunk);
    free(core->ring);
    memset(core, 0, sizeof(*core));
}

int audio_output_core_set_volume_db(audio_output_core_t *core, float volume_db)
{
    if (!core)
        return -1;

    if (volume_db > 0.0f)
        volume_db = 0.0f;
    if (volume_db < -144.0f)
        volume_db = -144.0f;

    core->volume_db = volume_db;
    if (volume_db <= -120.0f)
        core->volume_linear = 0.0f;
    else
        core->volume_linear = powf(10.0f, volume_db / 20.0f);

    return 0;
}

int audio_output_core_write(audio_output_core_t *core, const int16_t *samples, size_t sample_count)
{
    const int16_t *src;
    size_t count;
    size_t free_samples;
    int apply_gain;

    if (!core || !samples || sample_count == 0 || !core->ring || core->ring_capacity_samples == 0)
        return -1;

    src = samples;
    count = sample_count;
    apply_gain = (core->volume_linear < 0.9999f || core->volume_linear > 1.0001f);

    if (count > core->ring_capacity_samples)
    {
        src += (count - core->ring_capacity_samples);
        count = core->ring_capacity_samples;
    }

    free_samples = core->ring_capacity_samples - core->ring_fill_samples;
    if (count > free_samples)
    {
        size_t drop = count - free_samples;
        core->ring_read_pos = (core->ring_read_pos + drop) % core->ring_capacity_samples;
        core->ring_fill_samples -= drop;
    }

    if (!apply_gain)
    {
        ring_push_samples(core, src, count);
    }
    else
    {
        size_t i;
        for (i = 0; i < count; i++)
        {
            float scaled = (float)src[i] * core->volume_linear;
            int iv = (int)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
            core->ring[core->ring_write_pos] = clamp_i16(iv);
            core->ring_write_pos = (core->ring_write_pos + 1) % core->ring_capacity_samples;
        }
        core->ring_fill_samples += count;
    }

    if (core->ring_fill_samples > core->max_latency_samples)
    {
        size_t trim = core->ring_fill_samples - core->max_latency_samples;
        core->ring_read_pos = (core->ring_read_pos + trim) % core->ring_capacity_samples;
        core->ring_fill_samples -= trim;
    }

    core->in_samples_total += sample_count;
    return (int)sample_count;
}

int audio_output_core_pop_chunk(audio_output_core_t *core)
{
    size_t available;

    if (!core || !core->mix_chunk || core->chunk_samples == 0)
        return 0;

    if (!core->started)
    {
        if (core->ring_fill_samples >= core->preroll_samples)
            core->started = 1;
        else
            return 0;
    }

    if (core->ring_fill_samples >= core->chunk_samples)
    {
        ring_pop_samples(core, core->mix_chunk, core->chunk_samples);
        return 1;
    }

    available = core->ring_fill_samples;
    if (available > core->chunk_samples)
        available = core->chunk_samples;

    if (available > 0)
        ring_pop_samples(core, core->mix_chunk, available);

    if (available < core->chunk_samples)
        memset(core->mix_chunk + available, 0, (core->chunk_samples - available) * sizeof(int16_t));

    return 1;
}

void audio_output_core_on_chunk_played(audio_output_core_t *core)
{
    if (!core)
        return;

    core->out_samples_total += core->chunk_samples;
}

int audio_output_core_collect_stats(audio_output_core_t *core, uint32_t now_ms, audio_output_stats_t *stats)
{
    uint64_t in_delta;
    uint64_t out_delta;

    if (!core || !stats)
        return 0;

    if (core->stats_last_tick == 0)
        core->stats_last_tick = now_ms;

    if ((now_ms - core->stats_last_tick) < 1000)
        return 0;

    in_delta = core->in_samples_total - core->in_samples_last;
    out_delta = core->out_samples_total - core->out_samples_last;

    stats->in_frames_per_s = (core->channels > 0) ? (uint32_t)(in_delta / core->channels) : 0;
    stats->out_frames_per_s = (core->channels > 0) ? (uint32_t)(out_delta / core->channels) : 0;
    stats->ring_fill_ms = (core->sample_rate > 0 && core->channels > 0)
                              ? (uint32_t)((core->ring_fill_samples * 1000ULL) / ((uint64_t)core->sample_rate * core->channels))
                              : 0;

    core->in_samples_last = core->in_samples_total;
    core->out_samples_last = core->out_samples_total;
    core->stats_last_tick = now_ms;
    return 1;
}
