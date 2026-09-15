


#ifndef AIRPLAY_EMBEDDED_BOARD_AUDIO_H
#define AIRPLAY_EMBEDDED_BOARD_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* Board audio driver contract used by the generic embedded audio backend. */

/* Implement in the board project using its I2S/SAI/DMA driver. On failure,
 * open releases any partially created resources and leaves *context NULL. */
/**
 * @brief airplay_board_audio_open.
 * @param context Parameter named context.
 * @param sample_rate Parameter named sample_rate.
 * @param channels Parameter named channels.
 * @param bits_per_sample Parameter named bits_per_sample.
 * @return Function result.
 */
int airplay_board_audio_open(void **context,
                             uint32_t sample_rate,
                             uint8_t channels,
                             uint8_t bits_per_sample);
/* Signed interleaved 16-bit PCM. Copy or consume before returning; DMA must not
 * retain samples. Return sample_count or -1, within 200 ms including queue waits. */
/**
 * @brief airplay_board_audio_write.
 * @param context Parameter named context.
 * @param samples Parameter named samples.
 * @param sample_count Parameter named sample_count.
 * @return Function result.
 */
int airplay_board_audio_write(void *context, const int16_t *samples, size_t sample_count);
/* Remaining queued/DMA frames, including a partially consumed DMA buffer. */
/**
 * @brief airplay_board_audio_delay_frames.
 * @param context Parameter named context.
 * @return Function result.
 */
int airplay_board_audio_delay_frames(void *context);
/* Stop DMA and release resources; no callback may access context after return. */
/**
 * @brief airplay_board_audio_close.
 * @param context Parameter named context.
 */
void airplay_board_audio_close(void *context);

#endif
