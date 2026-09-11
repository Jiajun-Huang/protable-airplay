#define _POSIX_C_SOURCE 200809L
#include "audio.h"
#include "airplay_config.h"

#include <limits.h>
#include <stdlib.h>

#include <alsa/asoundlib.h>
#include <errno.h>
#include <time.h>

struct audio_device
{
    snd_pcm_t *pcm;
    uint8_t channels;
};

int audio_open(audio_device_t **device, uint32_t sample_rate,
               uint8_t channels, uint8_t bits)
{
    audio_device_t *output;
    if (!device)
        return -1;
    *device = NULL;
    if (!sample_rate || !channels || channels > 2 || bits != 16)
        return -1;
    output = (audio_device_t *)calloc(1, sizeof(*output));
    if (!output)
        return -1;
    if (snd_pcm_open(&output->pcm, "default", SND_PCM_STREAM_PLAYBACK,
                     SND_PCM_NONBLOCK) < 0)
    {
        free(output);
        return -1;
    }
    output->channels = channels;
    if (snd_pcm_set_params(output->pcm, SND_PCM_FORMAT_S16,
                           SND_PCM_ACCESS_RW_INTERLEAVED, channels,
                           sample_rate, 1, AIRPLAY_ALSA_BUFFER_US) < 0)
    {
        audio_close(output);
        return -1;
    }
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    if (snd_pcm_sw_params_current(output->pcm, sw) < 0 ||
        snd_pcm_sw_params_set_start_threshold(output->pcm, sw, 1) < 0 ||
        snd_pcm_sw_params(output->pcm, sw) < 0)
    {
        audio_close(output);
        return -1;
    }
    *device = output;
    return 0;
}

static uint64_t monotonic_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return 0;
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

int audio_delay_frames(audio_device_t *device)
{
    snd_pcm_sframes_t frames;
    if (!device)
        return -1;
    if (snd_pcm_state(device->pcm) == SND_PCM_STATE_XRUN)
        return 0;
    if (snd_pcm_delay(device->pcm, &frames) < 0)
        return -1;
    return frames < 0 ? 0 : frames > INT_MAX ? INT_MAX
                                             : (int)frames;
}

int audio_write(audio_device_t *device, const int16_t *samples, size_t sample_count)
{
    size_t written = 0;
    uint64_t deadline;
    unsigned attempts = 0;
    if (!device || (!samples && sample_count) || sample_count % device->channels ||
        sample_count > INT_MAX)
        return -1;
    deadline = monotonic_ms() + 120;
    while (written < sample_count)
    {
        snd_pcm_sframes_t frames = snd_pcm_writei(device->pcm, samples + written,
                                                  (sample_count - written) / device->channels);
        if (frames > 0)
        {
            written += (size_t)frames * device->channels;
        }
        else if (frames == -EPIPE || frames == -ESTRPIPE)
        {
            if (snd_pcm_prepare(device->pcm) < 0)
                return -1;
        }
        else if (frames == -EAGAIN || frames == -EINTR || frames == 0)
        {
            int result = snd_pcm_wait(device->pcm, 10);
            if (result < 0 && result != -EINTR && result != -EPIPE && result != -ESTRPIPE)
                return -1;
        }
        else
        {
            return -1;
        }
        if (written < sample_count && (monotonic_ms() >= deadline || ++attempts >= 64))
            return -1;
    }
    return (int)sample_count;
}

void audio_close(audio_device_t *device)
{
    if (!device)
        return;
    snd_pcm_drop(device->pcm);
    snd_pcm_close(device->pcm);
    free(device);
}
