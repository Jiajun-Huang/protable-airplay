#define _POSIX_C_SOURCE 200809L
#include "audio.h"
#include "airplay_config.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

struct audio_device
{
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[AIRPLAY_OUTPUT_BUFFER_COUNT];
    unsigned char busy[AIRPLAY_OUTPUT_BUFFER_COUNT];
    unsigned frames[AIRPLAY_OUTPUT_BUFFER_COUNT];
    pthread_mutex_t mutex;
    unsigned next;
    uint8_t channels;
    int started;
};

/**
 * @brief buffer_finished.
 * @param context Parameter named context.
 * @param queue Parameter named queue.
 * @param buffer Parameter named buffer.
 */
static void buffer_finished(void *context, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    audio_device_t *device = (audio_device_t *)context;
    unsigned i;
    (void)queue;
    pthread_mutex_lock(&device->mutex);
    for (i = 0; i < AIRPLAY_OUTPUT_BUFFER_COUNT; ++i)
    {
        if (device->buffers[i] == buffer)
        {
            device->busy[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&device->mutex);
}

int audio_open(audio_device_t **device, uint32_t sample_rate, uint8_t channels, uint8_t bits)
{
    audio_device_t *output;
    AudioStreamBasicDescription format = {0};
    unsigned i;
    if (!device)
        return -1;
    *device = NULL;
    if (!sample_rate || !channels || channels > 2 || bits != 16)
        return -1;
    output = (audio_device_t *)calloc(1, sizeof(*output));
    if (!output)
        return -1;
    if (pthread_mutex_init(&output->mutex, NULL) != 0)
    {
        free(output);
        return -1;
    }
    output->channels = channels;
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = channels * sizeof(int16_t);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBytesPerPacket;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = bits;
    if (AudioQueueNewOutput(&format, buffer_finished, output, NULL, NULL, 0, &output->queue) !=
        noErr)
    {
        pthread_mutex_destroy(&output->mutex);
        free(output);
        return -1;
    }
    for (i = 0; i < AIRPLAY_OUTPUT_BUFFER_COUNT; ++i)
    {
        if (AudioQueueAllocateBuffer(output->queue,
                                     AIRPLAY_OUTPUT_BUFFER_SAMPLES * sizeof(int16_t),
                                     &output->buffers[i]) != noErr)
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
    unsigned attempt;
    if (!device || (!samples && sample_count) || sample_count % device->channels ||
        sample_count > AIRPLAY_OUTPUT_BUFFER_SAMPLES)
        return -1;
    if (!sample_count)
        return 0;
    for (attempt = 0; attempt <= 60; ++attempt)
    {
        AudioQueueBufferRef buffer;
        unsigned index = device->next;
        pthread_mutex_lock(&device->mutex);
        if (!device->busy[index])
        {
            device->busy[index] = 1;
            device->frames[index] = (unsigned)(sample_count / device->channels);
            pthread_mutex_unlock(&device->mutex);
            buffer = device->buffers[index];
            memcpy(buffer->mAudioData, samples, sample_count * sizeof(*samples));
            buffer->mAudioDataByteSize = (UInt32)(sample_count * sizeof(*samples));
            if (AudioQueueEnqueueBuffer(device->queue, buffer, 0, NULL) != noErr)
            {
                pthread_mutex_lock(&device->mutex);
                device->busy[index] = 0;
                pthread_mutex_unlock(&device->mutex);
                return -1;
            }
            if (!device->started)
            {
                if (AudioQueueStart(device->queue, NULL) != noErr)
                    return -1;
                device->started = 1;
            }
            device->next = (index + 1) % AIRPLAY_OUTPUT_BUFFER_COUNT;
            return (int)sample_count;
        }
        pthread_mutex_unlock(&device->mutex);
        if (attempt != 60)
        {
            struct timespec delay = {0, 2000000L};
            nanosleep(&delay, NULL);
        }
    }
    return -1;
}

void audio_close(audio_device_t *device)
{
    if (!device)
        return;
    AudioQueueStop(device->queue, true);
    AudioQueueDispose(device->queue, true);
    pthread_mutex_destroy(&device->mutex);
    free(device);
}

int audio_delay_frames(audio_device_t *device)
{
    unsigned frames = 0;
    if (!device)
        return -1;
    pthread_mutex_lock(&device->mutex);
    for (unsigned i = 0; i < AIRPLAY_OUTPUT_BUFFER_COUNT; ++i)
        if (device->busy[i])
            frames += device->frames[i];
    pthread_mutex_unlock(&device->mutex);
    return frames > INT_MAX ? INT_MAX : (int)frames;
}
