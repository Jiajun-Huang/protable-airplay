#include "../audio_if.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <math.h>

// Stability-first profile: larger preroll and buffer reduce intermittent gaps.
// These values trade latency for smoother playback under packet jitter.
#define AUDIO_RING_SECONDS 3
#define AUDIO_CHUNK_FRAMES 352
#define AUDIO_PREROLL_MS 180
#define AUDIO_MAX_LATENCY_MS 320

typedef struct
{
    HWAVEOUT handle;
    uint8_t channels;
    uint8_t bits_per_sample;
    int initialized;
} win_audio_backend_t;

static win_audio_backend_t g_win_audio;

static int win_audio_init(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    WAVEFORMATEX wf;

    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = channels;
    wf.nSamplesPerSec = sample_rate;
    wf.wBitsPerSample = bits_per_sample;
    wf.nBlockAlign = (WORD)((wf.nChannels * wf.wBitsPerSample) / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    g_win_audio.handle = NULL;
    g_win_audio.channels = channels;
    g_win_audio.bits_per_sample = bits_per_sample;
    g_win_audio.initialized = 0;

    if (waveOutOpen(&g_win_audio.handle, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return -1;

    g_win_audio.initialized = 1;
    return 0;
}

static int win_audio_play_pcm(const int16_t *samples, size_t frame_count)
{
    WAVEHDR header;
    size_t bytes;
    int16_t *buffer_copy;

    if (!g_win_audio.initialized || !samples || frame_count == 0)
        return -1;

    bytes = frame_count * g_win_audio.channels * (g_win_audio.bits_per_sample / 8);
    buffer_copy = (int16_t *)malloc(bytes);
    if (!buffer_copy)
        return -1;

    memcpy(buffer_copy, samples, bytes);

    memset(&header, 0, sizeof(header));
    header.lpData = (LPSTR)buffer_copy;
    header.dwBufferLength = (DWORD)bytes;

    if (waveOutPrepareHeader(g_win_audio.handle, &header, sizeof(header)) != MMSYSERR_NOERROR)
    {
        free(buffer_copy);
        return -1;
    }

    if (waveOutWrite(g_win_audio.handle, &header, sizeof(header)) != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(g_win_audio.handle, &header, sizeof(header));
        free(buffer_copy);
        return -1;
    }

    while ((header.dwFlags & WHDR_DONE) == 0)
        Sleep(1);

    waveOutUnprepareHeader(g_win_audio.handle, &header, sizeof(header));
    free(buffer_copy);
    return 0;
}

static void win_audio_close(void)
{
    if (!g_win_audio.initialized)
        return;

    waveOutReset(g_win_audio.handle);
    waveOutClose(g_win_audio.handle);
    g_win_audio.handle = NULL;
    g_win_audio.initialized = 0;
}

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

    uint64_t in_samples_total;
    uint64_t out_samples_total;
    uint64_t in_samples_last;
    uint64_t out_samples_last;
    DWORD stats_last_tick;
    float volume_db;
    float volume_linear;
} audio_output_device;

static int16_t clamp_i16(int x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

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

        if (win_audio_play_pcm(device->mix_chunk, chunk_frames) != 0)
            Sleep(2);
        else
        {
            device->out_samples_total += chunk_samples;
            have_pending_chunk = 0;
        }
    }

    return 0;
}

audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    uint8_t effective_bits = 16;
    audio_output_device *device = (audio_output_device *)malloc(sizeof(*device));
    if (!device)
        return NULL;

    memset(device, 0, sizeof(*device));
    device->sample_rate = sample_rate;
    device->channels = channels;
    device->bits_per_sample = effective_bits;
    device->sample_count = 0;
    device->speaker_ready = (win_audio_init(sample_rate, channels, effective_bits) == 0) ? 1 : 0;
    device->volume_db = -20.0f;
    device->volume_linear = powf(10.0f, device->volume_db / 20.0f);

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
    printf("[audio_output] Output: %uHz, %u-ch, %u-bit (requested %u), speaker=%s\n",
           sample_rate, channels, effective_bits, bits_per_sample,
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
        int apply_gain = (device->volume_linear < 0.9999f || device->volume_linear > 1.0001f);

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

        if (!apply_gain)
        {
            ring_push_samples(device, src, count);
        }
        else
        {
            size_t i;
            for (i = 0; i < count; i++)
            {
                float scaled = (float)src[i] * device->volume_linear;
                int iv = (int)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
                device->ring[device->ring_write_pos] = clamp_i16(iv);
                device->ring_write_pos = (device->ring_write_pos + 1) % device->ring_capacity_samples;
            }
            device->ring_fill_samples += count;
        }

        size_t max_latency_samples = ((size_t)device->sample_rate * device->channels * AUDIO_MAX_LATENCY_MS) / 1000;
        if (device->ring_fill_samples > max_latency_samples)
        {
            size_t trim = device->ring_fill_samples - max_latency_samples;
            device->ring_read_pos = (device->ring_read_pos + trim) % device->ring_capacity_samples;
            device->ring_fill_samples -= trim;
        }

        LeaveCriticalSection(&device->lock);
    }

    device->in_samples_total += sample_count;

    {
        DWORD now = GetTickCount();
        if (device->stats_last_tick == 0)
            device->stats_last_tick = now;

        if (now - device->stats_last_tick >= 1000)
        {
            uint64_t in_delta = device->in_samples_total - device->in_samples_last;
            uint64_t out_delta = device->out_samples_total - device->out_samples_last;
            size_t fill_samples = 0;

            EnterCriticalSection(&device->lock);
            fill_samples = device->ring_fill_samples;
            LeaveCriticalSection(&device->lock);

            uint32_t in_frames_per_s = (device->channels > 0) ? (uint32_t)(in_delta / device->channels) : 0;
            uint32_t out_frames_per_s = (device->channels > 0) ? (uint32_t)(out_delta / device->channels) : 0;
            uint32_t fill_ms = (device->sample_rate > 0 && device->channels > 0)
                                   ? (uint32_t)((fill_samples * 1000ULL) / ((uint64_t)device->sample_rate * device->channels))
                                   : 0;

            printf("[audio_stats] in=%u fps out=%u fps ring=%u ms cfg=%uHz/%uch\\n",
                   in_frames_per_s,
                   out_frames_per_s,
                   fill_ms,
                   device->sample_rate,
                   device->channels);

            device->in_samples_last = device->in_samples_total;
            device->out_samples_last = device->out_samples_total;
            device->stats_last_tick = now;
        }
    }

    if (device->log_file)
    {
        size_t written = fwrite(samples, sizeof(int16_t), sample_count, device->log_file);
        device->sample_count += written;
    }

    return (int)sample_count;
}

int audio_output_set_volume_db(audio_output_device_t *dev, float volume_db)
{
    audio_output_device *device = (audio_output_device *)dev;
    if (!device)
        return -1;

    if (volume_db > 0.0f)
        volume_db = 0.0f;
    if (volume_db < -144.0f)
        volume_db = -144.0f;

    device->volume_db = volume_db;
    if (volume_db <= -120.0f)
        device->volume_linear = 0.0f;
    else
        device->volume_linear = powf(10.0f, volume_db / 20.0f);

    printf("[audio_output] Volume %.3f dB (x%.4f)\n", device->volume_db, device->volume_linear);
    return 0;
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
