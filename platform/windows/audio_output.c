#include "audio_output.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    FILE *log_file;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t sample_count;
    int speaker_ready;
} audio_output_device;

audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    audio_output_device *device = (audio_output_device *)malloc(sizeof(*device));
    if (!device)
        return NULL;
    device->sample_rate = sample_rate;
    device->channels = channels;
    device->bits_per_sample = bits_per_sample;
    device->sample_count = 0;
    device->speaker_ready = (win_audio_init(sample_rate, channels, bits_per_sample) == 0) ? 1 : 0;
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

    size_t written_total = 0;
    if (device->speaker_ready && device->channels > 0)
    {
        size_t frames_total = sample_count / device->channels;
        size_t consumed_samples = 0;

        while (frames_total > 0)
        {
            size_t chunk_frames = frames_total > 2048 ? 2048 : frames_total;
            if (win_audio_play_pcm(samples + consumed_samples, chunk_frames) == 0)
            {
                size_t advanced = chunk_frames * device->channels;
                consumed_samples += advanced;
                written_total += advanced;
                frames_total -= chunk_frames;
            }
            else
            {
                break;
            }
        }
    }

    if (device->log_file)
    {
        size_t written = fwrite(samples, sizeof(int16_t), sample_count, device->log_file);
        device->sample_count += written;
        if (written_total == 0)
            written_total = written;
    }

    return (int)written_total;
}

void audio_output_close(audio_output_device_t *dev)
{
    audio_output_device *device = (audio_output_device *)dev;
    if (!device)
        return;
    if (device->log_file)
    {
        fclose(device->log_file);
        printf("[audio_output] PCM written: %u samples to raop_audio.pcm\n", device->sample_count);
    }
    if (device->speaker_ready)
        win_audio_close();
    free(device);
}
