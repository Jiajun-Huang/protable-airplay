#include "audio.h"
#include "airplay_config.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct audio_device
{
    HWAVEOUT output;
    WAVEHDR headers[AIRPLAY_OUTPUT_BUFFER_COUNT];
    int16_t samples[AIRPLAY_OUTPUT_BUFFER_COUNT][AIRPLAY_OUTPUT_BUFFER_SAMPLES];
    unsigned next;
    uint8_t channels;
    uint32_t sample_rate, last_position;
    uint64_t submitted_frames, played_frames;
};

int audio_open(audio_device_t **device, uint32_t sample_rate,
               uint8_t channels, uint8_t bits)
{
    audio_device_t *output;
    WAVEFORMATEX format = {0};
    unsigned i;
    if (!device)
        return -1;
    *device = NULL;
    if (!sample_rate || !channels || channels > 2 || bits != 16)
        return -1;
    output = (audio_device_t *)calloc(1, sizeof(*output));
    if (!output)
        return -1;
    output->channels = channels;
    output->sample_rate = sample_rate;
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = bits;
    format.nBlockAlign = (WORD)(channels * sizeof(int16_t));
    format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
    if (waveOutOpen(&output->output, WAVE_MAPPER, &format, 0, 0,
                    CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        free(output);
        return -1;
    }
    for (i = 0; i < AIRPLAY_OUTPUT_BUFFER_COUNT; ++i)
    {
        output->headers[i].lpData = (LPSTR)output->samples[i];
        output->headers[i].dwBufferLength = sizeof(output->samples[i]);
        if (waveOutPrepareHeader(output->output, &output->headers[i],
                                 sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            audio_close(output);
            return -1;
        }
    }
    *device = output;
    return 0;
}

int audio_write(audio_device_t *device, const int16_t *samples, size_t sample_count)
{
    ULONGLONG deadline;
    if (!device || (!samples && sample_count) || sample_count % device->channels ||
        sample_count > AIRPLAY_OUTPUT_BUFFER_SAMPLES)
        return -1;
    if (!sample_count)
        return 0;
    deadline = GetTickCount64() + 120;
    for (;;)
    {
        WAVEHDR *header = &device->headers[device->next];
        if (!(header->dwFlags & WHDR_INQUEUE))
        {
            memcpy(header->lpData, samples, sample_count * sizeof(*samples));
            header->dwBufferLength = (DWORD)(sample_count * sizeof(*samples));
            if (waveOutWrite(device->output, header, sizeof(*header)) != MMSYSERR_NOERROR)
                return -1;
            device->next = (device->next + 1) % AIRPLAY_OUTPUT_BUFFER_COUNT;
            device->submitted_frames += sample_count / device->channels;
            return (int)sample_count;
        }
        if (GetTickCount64() >= deadline)
            break;
        Sleep(2);
    }
    return -1;
}

int audio_delay_frames(audio_device_t *device)
{
    MMTIME position = {0};
    position.wType = TIME_SAMPLES;
    if (!device || waveOutGetPosition(device->output, &position, sizeof(position)) != MMSYSERR_NOERROR)
        return -1;
    uint32_t frames;
    if (position.wType == TIME_SAMPLES)
        frames = position.u.sample;
    else if (position.wType == TIME_BYTES)
        frames = position.u.cb / (2 * device->channels);
    else if (position.wType == TIME_MS)
        frames = (uint32_t)((uint64_t)position.u.ms * device->sample_rate / 1000);
    else
        return -1;
    device->played_frames += (uint32_t)(frames - device->last_position);
    device->last_position = frames;
    uint64_t delay = device->submitted_frames > device->played_frames ? device->submitted_frames - device->played_frames : 0;
    return delay > INT_MAX ? INT_MAX : (int)delay;
}

void audio_close(audio_device_t *device)
{
    unsigned i;
    if (!device)
        return;
    waveOutReset(device->output);
    for (i = 0; i < AIRPLAY_OUTPUT_BUFFER_COUNT; ++i)
    {
        if (device->headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(device->output, &device->headers[i], sizeof(WAVEHDR));
    }
    waveOutClose(device->output);
    free(device);
}
