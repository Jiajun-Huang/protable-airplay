#include "../audio_if.h"
#include "audio_output_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

#define AUDIO_RING_SECONDS 3
#define AUDIO_CHUNK_FRAMES 352
#define AUDIO_PREROLL_MS 180
#define AUDIO_MAX_LATENCY_MS 320

typedef struct
{
    HWAVEOUT handle;
    uint8_t channels;
    uint8_t bits_per_sample;
    WAVEHDR headers[8];
    uint8_t buffers[8][32768];
    int current_buffer;
    int initialized;
} win_audio_backend_t;

static win_audio_backend_t g_win_audio;

static int win_audio_init(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    MMRESULT result;
    int i;
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
    g_win_audio.current_buffer = 0;
    g_win_audio.initialized = 0;

    result = waveOutOpen(&g_win_audio.handle, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
        return -1;

    for (i = 0; i < 8; i++)
    {
        memset(&g_win_audio.headers[i], 0, sizeof(WAVEHDR));
        g_win_audio.headers[i].lpData = (LPSTR)g_win_audio.buffers[i];
        g_win_audio.headers[i].dwBufferLength = sizeof(g_win_audio.buffers[i]);

        result = waveOutPrepareHeader(g_win_audio.handle, &g_win_audio.headers[i], sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            int j;
            for (j = 0; j < i; j++)
                waveOutUnprepareHeader(g_win_audio.handle, &g_win_audio.headers[j], sizeof(WAVEHDR));

            waveOutClose(g_win_audio.handle);
            g_win_audio.handle = NULL;
            return -1;
        }
    }

    g_win_audio.initialized = 1;
    return 0;
}

static int win_audio_play_pcm(const int16_t *samples, size_t frame_count)
{
    size_t bytes;
    int wait_ms;
    int n;
    int selected = -1;
    WAVEHDR *header;

    if (!g_win_audio.initialized || !samples || frame_count == 0)
        return -1;

    bytes = frame_count * g_win_audio.channels * (g_win_audio.bits_per_sample / 8);
    if (bytes > sizeof(g_win_audio.buffers[0]))
        return -1;

    for (wait_ms = 0; wait_ms < 120 && selected < 0; wait_ms += 2)
    {
        for (n = 0; n < 8; n++)
        {
            int idx = (g_win_audio.current_buffer + n) % 8;
            if ((g_win_audio.headers[idx].dwFlags & WHDR_INQUEUE) == 0)
            {
                selected = idx;
                break;
            }
        }

        if (selected < 0)
            Sleep(2);
    }

    if (selected < 0)
        return -1;

    header = &g_win_audio.headers[selected];
    memcpy(header->lpData, samples, bytes);
    header->dwBufferLength = (DWORD)bytes;
    header->dwFlags &= ~WHDR_DONE;

    if (waveOutWrite(g_win_audio.handle, header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        return -1;

    g_win_audio.current_buffer = (selected + 1) % 8;
    return 0;
}

static void win_audio_close(void)
{
    int i;
    if (!g_win_audio.initialized)
        return;

    waveOutReset(g_win_audio.handle);

    for (i = 0; i < 8; i++)
    {
        if (g_win_audio.headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(g_win_audio.handle, &g_win_audio.headers[i], sizeof(WAVEHDR));
    }

    waveOutClose(g_win_audio.handle);
    g_win_audio.handle = NULL;
    g_win_audio.initialized = 0;
}

typedef struct audio_output_device
{
    FILE *log_file;
    uint32_t sample_count;
    int speaker_ready;

    CRITICAL_SECTION lock;
    HANDLE play_thread;
    volatile LONG stop_thread;

    audio_output_core_t core;
    int16_t *ring_storage;
    size_t ring_storage_samples;
    int16_t *mix_storage;
    size_t mix_storage_samples;
} audio_output_device;

static DWORD WINAPI audio_play_thread_proc(LPVOID param)
{
    audio_output_device_t *device = (audio_output_device_t *)param;
    int have_pending_chunk = 0;
    if (!device)
        return 0;

    while (InterlockedCompareExchange((LONG *)&device->stop_thread, 0, 0) == 0)
    {
        if (!have_pending_chunk)
        {
            int has_chunk = 0;
            EnterCriticalSection(&device->lock);
            has_chunk = audio_output_core_pop_chunk(&device->core);
            LeaveCriticalSection(&device->lock);

            if (!has_chunk)
            {
                Sleep(2);
                continue;
            }

            have_pending_chunk = 1;
        }

        if (win_audio_play_pcm(device->core.mix_chunk, device->core.chunk_frames) != 0)
        {
            Sleep(2);
            continue;
        }

        EnterCriticalSection(&device->lock);
        audio_output_core_on_chunk_played(&device->core);
        LeaveCriticalSection(&device->lock);
        have_pending_chunk = 0;
    }

    return 0;
}

audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    uint8_t effective_bits = 16;
    size_t ring_samples;
    size_t chunk_samples;
    audio_output_device_t *device = (audio_output_device_t *)malloc(sizeof(*device));

    if (!device)
        return NULL;

    memset(device, 0, sizeof(*device));
    InitializeCriticalSection(&device->lock);

    ring_samples = (size_t)sample_rate * channels * AUDIO_RING_SECONDS;
    chunk_samples = (size_t)AUDIO_CHUNK_FRAMES * channels;

    device->ring_storage = (int16_t *)malloc(ring_samples * sizeof(int16_t));
    device->mix_storage = (int16_t *)malloc(chunk_samples * sizeof(int16_t));
    device->ring_storage_samples = ring_samples;
    device->mix_storage_samples = chunk_samples;

    if (!device->ring_storage || !device->mix_storage)
    {
        DeleteCriticalSection(&device->lock);
        free(device->mix_storage);
        free(device->ring_storage);
        free(device);
        return NULL;
    }

    if (audio_output_core_init(&device->core,
                               sample_rate,
                               channels,
                               effective_bits,
                               AUDIO_RING_SECONDS,
                               device->ring_storage,
                               device->ring_storage_samples,
                               AUDIO_CHUNK_FRAMES,
                               device->mix_storage,
                               device->mix_storage_samples,
                               AUDIO_PREROLL_MS,
                               AUDIO_MAX_LATENCY_MS) != 0)
    {
        DeleteCriticalSection(&device->lock);
        free(device->mix_storage);
        free(device->ring_storage);
        free(device);
        return NULL;
    }

    device->speaker_ready = (win_audio_init(sample_rate, channels, effective_bits) == 0) ? 1 : 0;

    if (device->speaker_ready)
    {
        device->play_thread = CreateThread(NULL, 0, audio_play_thread_proc, device, 0, NULL);
        if (!device->play_thread)
            device->speaker_ready = 0;
    }

    audio_output_core_set_volume_db(&device->core, -20.0f);

    device->log_file = fopen("raop_audio.pcm", "wb");
    printf("[audio_output] Output: %uHz, %u-ch, %u-bit (requested %u), speaker=%s\n",
           sample_rate,
           channels,
           effective_bits,
           bits_per_sample,
           device->speaker_ready ? "ready" : "failed");

    return device;
}

int audio_output_write(audio_output_device_t *device, const int16_t *samples, size_t sample_count)
{
    audio_output_stats_t stats;
    int written;

    if (!device || !samples || sample_count == 0)
        return -1;

    EnterCriticalSection(&device->lock);
    written = audio_output_core_write(&device->core, samples, sample_count);
    if (audio_output_core_collect_stats(&device->core, GetTickCount(), &stats))
    {
        printf("[audio_stats] in=%u fps out=%u fps ring=%u ms cfg=%uHz/%uch\\n",
               stats.in_frames_per_s,
               stats.out_frames_per_s,
               stats.ring_fill_ms,
               device->core.sample_rate,
               device->core.channels);
    }
    LeaveCriticalSection(&device->lock);

    if (device->log_file)
    {
        size_t logged = fwrite(samples, sizeof(int16_t), sample_count, device->log_file);
        device->sample_count += (uint32_t)logged;
    }

    return written;
}

int audio_output_set_volume_db(audio_output_device_t *device, float volume_db)
{
    int rc;

    if (!device)
        return -1;

    EnterCriticalSection(&device->lock);
    rc = audio_output_core_set_volume_db(&device->core, volume_db);
    printf("[audio_output] Volume %.3f dB (x%.4f)\n", device->core.volume_db, device->core.volume_linear);
    LeaveCriticalSection(&device->lock);

    return rc;
}

void audio_output_close(audio_output_device_t *device)
{
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

    EnterCriticalSection(&device->lock);
    audio_output_core_deinit(&device->core);
    LeaveCriticalSection(&device->lock);

    DeleteCriticalSection(&device->lock);
    free(device->mix_storage);
    free(device->ring_storage);
    free(device);
}
