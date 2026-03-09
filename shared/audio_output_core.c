#include "audio_output_core.h"

#include <math.h>
#include <string.h>

static int16_t clamp_i16(int x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

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
                           uint32_t max_latency_ms)
{
    size_t required_ring_samples;

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
    required_ring_samples = (size_t)sample_rate * channels * ring_seconds;
    if (!ring_buffer || ring_buffer_samples < required_ring_samples)
        return -1;

    if (!mix_buffer || mix_buffer_samples < core->chunk_samples)
        return -1;

    if (spsc_ring_init(&core->ring, ring_buffer, required_ring_samples) != 0)
        return -1;

    core->mix_chunk = mix_buffer;

    core->volume_db = 0.0f;
    core->volume_linear = 1.0f;
    return 0;
}

void audio_output_core_deinit(audio_output_core_t *core)
{
    if (!core)
        return;

    spsc_ring_deinit(&core->ring);
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
    int16_t scaled_block[256];

    if (!core || !samples || sample_count == 0 || !core->ring.buffer || spsc_ring_capacity(&core->ring) == 0)
        return -1;

    src = samples;
    count = sample_count;
    apply_gain = (core->volume_linear < 0.9999f || core->volume_linear > 1.0001f);

    if (count > spsc_ring_capacity(&core->ring))
    {
        src += (count - spsc_ring_capacity(&core->ring));
        count = spsc_ring_capacity(&core->ring);
    }

    free_samples = spsc_ring_free(&core->ring);
    if (count > free_samples)
    {
        size_t drop = count - free_samples;
        spsc_ring_drop_oldest(&core->ring, drop);
    }

    if (!apply_gain)
    {
        spsc_ring_push(&core->ring, src, count);
    }
    else
    {
        size_t remaining = count;
        while (remaining > 0)
        {
            size_t block = remaining;
            size_t i;
            if (block > (sizeof(scaled_block) / sizeof(scaled_block[0])))
                block = sizeof(scaled_block) / sizeof(scaled_block[0]);

            for (i = 0; i < block; i++)
            {
                float scaled = (float)src[i] * core->volume_linear;
                int iv = (int)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
                scaled_block[i] = clamp_i16(iv);
            }

            spsc_ring_push(&core->ring, scaled_block, block);
            src += block;
            remaining -= block;
        }
    }

    if (spsc_ring_fill(&core->ring) > core->max_latency_samples)
    {
        size_t trim = spsc_ring_fill(&core->ring) - core->max_latency_samples;
        spsc_ring_drop_oldest(&core->ring, trim);
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
        if (spsc_ring_fill(&core->ring) >= core->preroll_samples)
            core->started = 1;
        else
            return 0;
    }

    if (spsc_ring_fill(&core->ring) >= core->chunk_samples)
    {
        spsc_ring_pop(&core->ring, core->mix_chunk, core->chunk_samples);
        return 1;
    }

    available = spsc_ring_fill(&core->ring);
    if (available > core->chunk_samples)
        available = core->chunk_samples;

    if (available > 0)
        spsc_ring_pop(&core->ring, core->mix_chunk, available);

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
                              ? (uint32_t)((spsc_ring_fill(&core->ring) * 1000ULL) / ((uint64_t)core->sample_rate * core->channels))
                              : 0;

    core->in_samples_last = core->in_samples_total;
    core->out_samples_last = core->out_samples_total;
    core->stats_last_tick = now_ms;
    return 1;
}
