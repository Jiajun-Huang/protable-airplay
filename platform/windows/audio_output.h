#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Windows audio output device
 * Plays PCM audio to the default speaker using WaveOut.
 */

typedef struct audio_output_device audio_output_device_t;

/**
 * @brief Create and initialize audio output device
 * @param sample_rate Sample rate (e.g., 44100, 48000)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param bits_per_sample Bits per sample (typically 16)
 * @return device handle on success, NULL on error
 */
audio_output_device_t *audio_output_create(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);

/**
 * @brief Write PCM samples to audio output
 * @param device audio device
 * @param samples PCM samples (int16_t format)
 * @param sample_count number of samples to write
 * @return number of samples written, or negative on error
 */
int audio_output_write(audio_output_device_t *device, const int16_t *samples, size_t sample_count);

/**
 * @brief Close and release audio device
 * @param device audio device
 */
void audio_output_close(audio_output_device_t *device);

#endif // AUDIO_OUTPUT_H
