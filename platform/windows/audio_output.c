#include "audio_output.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <string.h>

// Stability-first profile: larger preroll and buffer reduce intermittent gaps.
#define AUDIO_RING_SECONDS 3
#define AUDIO_CHUNK_FRAMES 352
#define AUDIO_PREROLL_MS 180
#define AUDIO_MAX_LATENCY_MS 320

typedef struct
{
    FILE *log_file;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t sample_count;
    int speaker_ready;

    int16_t *ring;
    size_t ring_capacity_samples;
    size_t ring_read_pos;
    size_t ring_write_pos;
    size_t ring_fill_samples;

    int16_t *mix_chunk;
    size_t mix_chunk_samples;

    CRITICAL_SECTION lock;
    HANDLE play_thread;
    volatile LONG stop_thread;
} audio_output_device;

static void ring_pop_samples(audio_output_device *device, int16_t *dst, size_t count)
{
    size_t first = count;
    if (device->ring_read_pos + first > device->ring_capacity_samples)
        first = device->ring_capacity_samples - device->ring_read_pos;

    memcpy(dst, device->ring + device->ring_read_pos, first * sizeof(int16_t));

    if (count > first)
        memcpy(dst + first, device->ring, (count - first) * sizeof(int16_t));

    device->ring_read_pos = (device->ring_read_pos + count) % device->ring_capacity_samples;
    device->ring_fill_samples -= count;
}

static void ring_push_samples(audio_output_device *device, const int16_t *src, size_t count)
{
    size_t first = count;
    if (device->ring_write_pos + first > device->ring_capacity_samples)
        first = device->ring_capacity_samples - device->ring_write_pos;

    memcpy(device->ring + device->ring_write_pos, src, first * sizeof(int16_t));

    if (count > first)
        memcpy(device->ring, src + first, (count - first) * sizeof(int16_t));

    device->ring_write_pos = (device->ring_write_pos + count) % device->ring_capacity_samples;
    device->ring_fill_samples += count;
}

static DWORD WINAPI audio_play_thread_proc(LPVOID param)
{
    audio_output_device *device = (audio_output_device *)param;
    if (!device)
        return 0;

    size_t chunk_frames = AUDIO_CHUNK_FRAMES;
    size_t chunk_samples = chunk_frames * device->channels;
    size_t preroll_samples = (device->sample_rate * device->channels * AUDIO_PREROLL_MS) / 1000;
    int started = 0;
    int have_pending_chunk = 0;

    while (InterlockedCompareExchange((LONG *)&device->stop_thread, 0, 0) == 0)
    {
        if (!have_pending_chunk)
        {
            EnterCriticalSection(&device->lock);
            if (!started)
            {
                if (device->ring_fill_samples >= preroll_samples)
                    started = 1;
            }

            if (started && device->ring_fill_samples >= chunk_samples)
            {
                ring_pop_samples(device, device->mix_chunk, chunk_samples);
                have_pending_chunk = 1;
            }
            else if (started)
            {
                // Keep output paced in real-time: play available samples and pad the rest with silence.
                size_t available = device->ring_fill_samples;
                if (available > chunk_samples)
                    available = chunk_samples;

                if (available > 0)
                    ring_pop_samples(device, device->mix_chunk, available);

                if (available < chunk_samples)
                {
                    memset(device->mix_chunk + available, 0,
                           (chunk_samples - available) * sizeof(int16_t));
                }

                have_pending_chunk = 1;
            }
            LeaveCriticalSection(&device->lock);

            if (!have_pending_chunk)
            {
                Sleep(2);
                continue;
            }
        }

        // Retry the same chunk on temporary output saturation instead of dropping it.
        if (win_audio_play_pcm(device->mix_chunk, chunk_frames) != 0)
            Sleep(2);
        else
            have_pending_chunk = 0;
    }

    return 0;
}

audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    audio_output_device *device = (audio_output_device *)malloc(sizeof(*device));
    if (!device)
        return NULL;

    memset(device, 0, sizeof(*device));
    device->sample_rate = sample_rate;
    device->channels = channels;
    device->bits_per_sample = bits_per_sample;
    device->sample_count = 0;
    device->speaker_ready = (win_audio_init(sample_rate, channels, bits_per_sample) == 0) ? 1 : 0;

    InitializeCriticalSection(&device->lock);

    if (channels > 0)
    {
        device->ring_capacity_samples = (size_t)sample_rate * channels * AUDIO_RING_SECONDS;
        device->ring = (int16_t *)malloc(device->ring_capacity_samples * sizeof(int16_t));
        device->mix_chunk_samples = (size_t)AUDIO_CHUNK_FRAMES * channels;
        device->mix_chunk = (int16_t *)malloc(device->mix_chunk_samples * sizeof(int16_t));
    }

    if (device->speaker_ready && device->ring && device->mix_chunk)
    {
        device->play_thread = CreateThread(NULL, 0, audio_play_thread_proc, device, 0, NULL);
        if (!device->play_thread)
            device->speaker_ready = 0;
    }

    device->log_file = fopen("raop_audio.pcm", "wb");
    printf("[audio_output] Output: %uHz, %u-ch, %u-bit, speaker=%s\n",
           sample_rate, channels, bits_per_sample,
           device->speaker_ready ? "ready" : "failed");
    return (audio_output_device_t *)device;
}

int audio_output_write(audio_output_device_t *dev, const int16_t *samples, size_t sample_count)
{
    audio_output_device *device = (audio_output_device *)dev;
    if (!device || !samples || sample_count == 0)
        return -1;

    if (device->speaker_ready && device->channels > 0 && device->ring && device->ring_capacity_samples > 0)
    {
        const int16_t *src = samples;
        size_t count = sample_count;

        if (count > device->ring_capacity_samples)
        {
            src += (count - device->ring_capacity_samples);
            count = device->ring_capacity_samples;
        }

        EnterCriticalSection(&device->lock);

        size_t free_samples = device->ring_capacity_samples - device->ring_fill_samples;
        if (count > free_samples)
        {
            size_t drop = count - free_samples;
            device->ring_read_pos = (device->ring_read_pos + drop) % device->ring_capacity_samples;
            device->ring_fill_samples -= drop;
        }

        ring_push_samples(device, src, count);

        // Cap end-to-end latency by trimming oldest queued audio.
        size_t max_latency_samples = ((size_t)device->sample_rate * device->channels * AUDIO_MAX_LATENCY_MS) / 1000;
        if (device->ring_fill_samples > max_latency_samples)
        {
            size_t trim = device->ring_fill_samples - max_latency_samples;
            device->ring_read_pos = (device->ring_read_pos + trim) % device->ring_capacity_samples;
            device->ring_fill_samples -= trim;
        }

        LeaveCriticalSection(&device->lock);
    }

    if (device->log_file)
    {
        size_t written = fwrite(samples, sizeof(int16_t), sample_count, device->log_file);
        device->sample_count += written;
    }

    return (int)sample_count;
}

void audio_output_close(audio_output_device_t *dev)
{
    audio_output_device *device = (audio_output_device *)dev;
    if (!device)
        return;

    InterlockedExchange((LONG *)&device->stop_thread, 1);
    if (device->play_thread)
    {
        WaitForSingleObject(device->play_thread, 1000);
        CloseHandle(device->play_thread);
        device->play_thread = NULL;
    }

    if (device->log_file)
    {
        fclose(device->log_file);
        printf("[audio_output] PCM written: %u samples to raop_audio.pcm\n", device->sample_count);
    }
    if (device->speaker_ready)
        win_audio_close();

    DeleteCriticalSection(&device->lock);
    free(device->mix_chunk);
    free(device->ring);
    free(device);
}
