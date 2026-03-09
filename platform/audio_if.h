#ifndef AUDIO_IF_H
#define AUDIO_IF_H

#include <stdint.h>
#include <stddef.h>

typedef struct audio_output_device audio_output_device_t;

audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
int audio_output_write(audio_output_device_t *device, const int16_t *samples, size_t sample_count);
int audio_output_set_volume_db(audio_output_device_t *device, float volume_db);
void audio_output_close(audio_output_device_t *device);

#endif // AUDIO_IF_H
