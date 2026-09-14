#include "audio.h"
#include "board_audio.h"

#include "FreeRTOS.h"
#include <limits.h>

struct audio_device
{
    void *context;
    uint8_t channels;
};

int audio_open(audio_device_t **device,
               uint32_t sample_rate,
               uint8_t channels,
               uint8_t bits_per_sample)
{
    audio_device_t *output;
    if (!device)
        return -1;
    *device = NULL;
    if (!sample_rate || !channels || channels > 2 || bits_per_sample != 16)
        return -1;
    output = (audio_device_t *)pvPortMalloc(sizeof(*output));
    if (!output)
        return -1;
    output->context = NULL;
    output->channels = channels;
    if (airplay_board_audio_open(&output->context, sample_rate, channels, bits_per_sample) != 0)
    {
        vPortFree(output);
        return -1;
    }
    *device = output;
    return 0;
}

int audio_write(audio_device_t *device, const int16_t *samples, size_t sample_count)
{
    int result;
    if (!device || (!samples && sample_count) || sample_count % device->channels ||
        sample_count > INT_MAX)
        return -1;
    if (!sample_count)
        return 0;
    result = airplay_board_audio_write(device->context, samples, sample_count);
    return result == (int)sample_count ? result : -1;
}

void audio_close(audio_device_t *device)
{
    if (!device)
        return;
    airplay_board_audio_close(device->context);
    vPortFree(device);
}

int audio_delay_frames(audio_device_t *device)
{
    return device ? airplay_board_audio_delay_frames(device->context) : -1;
}
