

#ifndef AAC_DECODER_H
#define AAC_DECODER_H

#include <stddef.h>
#include <stdint.h>

/* AAC decoder adapter backed by FDK-AAC. */

/* Caller-owned decoder handle and negotiated output format. */
typedef struct
{
    void *impl;
    uint8_t channels;
    uint32_t sample_rate;
} aac_decoder_t;

/* Initialize the decoder from MPEG-4 AudioSpecificConfig bytes. */
/**
 * @brief aac_decoder_init.
 * @param decoder Parameter named decoder.
 * @param config Parameter named config.
 * @param config_len Parameter named config_len.
 * @param channels Parameter named channels.
 * @param sample_rate Parameter named sample_rate.
 * @return Function result.
 */
int aac_decoder_init(aac_decoder_t *decoder,
                     const uint8_t *config,
                     size_t config_len,
                     uint8_t channels,
                     uint32_t sample_rate);
/* Decode one AAC access unit into interleaved signed 16-bit PCM samples. */
/**
 * @brief aac_decoder_decode.
 * @param decoder Parameter named decoder.
 * @param input Parameter named input.
 * @param input_len Parameter named input_len.
 * @param output Parameter named output.
 * @param output_capacity_samples Parameter named output_capacity_samples.
 * @param output_samples Parameter named output_samples.
 * @return Function result.
 */
int aac_decoder_decode(aac_decoder_t *decoder,
                       const uint8_t *input,
                       size_t input_len,
                       int16_t *output,
                       size_t output_capacity_samples,
                       size_t *output_samples);
/* Release the decoder implementation and reset its state. */
/**
 * @brief aac_decoder_close.
 * @param decoder Parameter named decoder.
 */
void aac_decoder_close(aac_decoder_t *decoder);

#endif
