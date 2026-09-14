#ifndef AIRPLAY_AUDIO_H
#define AIRPLAY_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* Platform audio output interface. The shared audio service sends decoded,
 * volume-adjusted PCM through this API without depending on a native driver. */

/* Opaque platform-owned audio device and buffer state. */
typedef struct audio_device audio_device_t;

/* Open one output device for the negotiated PCM format. */
int audio_open(audio_device_t **device,
               uint32_t sample_rate,
               uint8_t channels,
               uint8_t bits_per_sample);
/* Interleaved signed 16-bit PCM. Copy/consume before returning; never retain samples.
 * Return sample_count on success, -1 on failure; block for at most 200 ms.
 * Samples have already had the stream volume applied by shared/audio/audio_pipeline.c. */
int audio_write(audio_device_t *device, const int16_t *samples, size_t sample_count);
/* Estimated frames submitted but not yet played (one frame contains all channels).
 * Nonnegative on success, -1 on device error. Does not block. */
int audio_delay_frames(audio_device_t *device);
/* Stop playback and release all platform-owned audio resources. */
void audio_close(audio_device_t *device);

#endif
