#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

/**
 * @brief Windows audio output using WaveOut API
 * Simple, compatible approach using Multimedia Audio API
 * This provides basic 16-bit PCM playback to default audio device
 */

// Audio device handle
static HWAVEOUT hWaveOut = NULL;
#define WAVEOUT_BUFFER_COUNT 8
#define WAVEOUT_BUFFER_BYTES 32768
static WAVEHDR waveHeaders[WAVEOUT_BUFFER_COUNT];
static uint8_t audioBuffers[WAVEOUT_BUFFER_COUNT][WAVEOUT_BUFFER_BYTES];
static int currentBuffer = 0;
static float volume_linear = 0.8f;
static uint16_t g_channels = 2;
static uint16_t g_bits_per_sample = 16;

int win_audio_init(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    if (hWaveOut != NULL)
    {
        printf("[audio] Audio already initialized\n");
        return 0;
    }

    // Prepare wave format
    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = bits_per_sample;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    // Open waveform audio output device
    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
        fprintf(stderr, "[audio] Failed to open waveform output device: %d\n", result);
        return -1;
    }

    // Prepare wave headers and buffers
    for (int i = 0; i < WAVEOUT_BUFFER_COUNT; i++)
    {
        memset(&waveHeaders[i], 0, sizeof(WAVEHDR));
        waveHeaders[i].lpData = (LPSTR)audioBuffers[i];
        waveHeaders[i].dwBufferLength = sizeof(audioBuffers[i]);
        waveHeaders[i].dwFlags = 0;

        result = waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            fprintf(stderr, "[audio] Failed to prepare wave header %d: %d\n", i, result);
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
            return -1;
        }
    }

    currentBuffer = 0;
    g_channels = channels;
    g_bits_per_sample = bits_per_sample;
    printf("[audio] Initialized: %u Hz, %u channels, %u bits\n", sample_rate, channels, bits_per_sample);
    return 0;
}

int win_audio_play_pcm(const int16_t *samples, size_t frames)
{
    if (!hWaveOut || !samples || frames == 0)
        return -1;

    // Calculate buffer size for the current audio format.
    size_t bytes_per_frame = ((size_t)g_channels * (size_t)g_bits_per_sample) / 8;
    size_t bytes_needed = frames * bytes_per_frame;
    if (bytes_needed > sizeof(audioBuffers[0]))
    {
        fprintf(stderr, "[audio] Frame size too large: %zu bytes\n", bytes_needed);
        return -1;
    }

    // Find a free prepared buffer. Wait briefly instead of dropping immediately.
    WAVEHDR *pHeader = NULL;
    int selected_index = -1;
    for (int wait_ms = 0; wait_ms < 120 && !pHeader; wait_ms += 2)
    {
        for (int n = 0; n < WAVEOUT_BUFFER_COUNT; n++)
        {
            int idx = (currentBuffer + n) % WAVEOUT_BUFFER_COUNT;
            if ((waveHeaders[idx].dwFlags & WHDR_INQUEUE) == 0)
            {
                pHeader = &waveHeaders[idx];
                selected_index = idx;
                break;
            }
        }

        if (!pHeader)
            Sleep(2);
    }

    if (!pHeader)
    {
        static int underrun_log_count = 0;
        if ((underrun_log_count++ % 50) == 0)
            printf("[audio] Output buffers saturated, skipping chunk\n");
        return -1;
    }

    // Copy PCM data with volume adjustment
    int16_t *pBuffer = (int16_t *)pHeader->lpData;
    for (size_t i = 0; i < frames * g_channels; i++)
    {
        // Apply volume as linear gain
        float sample_f = (float)samples[i] * volume_linear;
        // Clamp to int16 range
        if (sample_f > 32767.0f)
            pBuffer[i] = 32767;
        else if (sample_f < -32768.0f)
            pBuffer[i] = -32768;
        else
            pBuffer[i] = (int16_t)sample_f;
    }

    // Set actual data size
    pHeader->dwBufferLength = bytes_needed;
    pHeader->dwFlags &= ~WHDR_DONE;

    // Queue the buffer
    MMRESULT result = waveOutWrite(hWaveOut, pHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        fprintf(stderr, "[audio] Failed to write wave data: %d\n", result);
        return -1;
    }

    // Move to next buffer
    currentBuffer = (selected_index + 1) % WAVEOUT_BUFFER_COUNT;

    return 0;
}

int win_audio_set_volume_db(float volume_db)
{
    // Convert dB to linear (dB = 20 * log10(linear))
    // linear = 10^(dB/20)
    volume_linear = powf(10.0f, volume_db / 20.0f);

    // Clamp to reasonable range [0, 2.0]
    if (volume_linear < 0.0f)
        volume_linear = 0.0f;
    if (volume_linear > 2.0f)
        volume_linear = 2.0f;

    printf("[audio] Volume set to %.1f dB (linear: %.3f)\n", volume_db, volume_linear);
    return 0;
}

int win_audio_flush(void)
{
    if (!hWaveOut)
        return -1;

    waveOutReset(hWaveOut);
    return 0;
}

void win_audio_close(void)
{
    if (!hWaveOut)
        return;

    // Stop playback
    waveOutReset(hWaveOut);

    // Unprepare headers
    for (int i = 0; i < WAVEOUT_BUFFER_COUNT; i++)
    {
        if (waveHeaders[i].dwFlags & WHDR_PREPARED)
        {
            waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        }
    }

    // Close device
    waveOutClose(hWaveOut);
    hWaveOut = NULL;

    printf("[audio] Closed audio output\n");
}

// Callback handlers (stubs for now)
void win_audio_on_volume_db(float volume_db, void *user_data)
{
    (void)user_data;
    win_audio_set_volume_db(volume_db);
}

void win_audio_on_progress(uint32_t start, uint32_t current, uint32_t end, void *user_data)
{
    (void)user_data;
    (void)start;
    (void)current;
    (void)end;
    // Progress callback - could update UI here
}

void win_audio_on_stream_state(const char *state, void *user_data)
{
    (void)user_data;
    printf("[audio] Stream state: %s\n", state);
}
