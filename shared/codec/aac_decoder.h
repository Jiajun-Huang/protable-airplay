#ifndef AAC_DECODER_H
#define AAC_DECODER_H

#include <stddef.h>
#include <stdint.h>

/* AAC decoder adapter. Desktop builds wrap FDK-AAC while embedded builds may
 * provide the same contract with a board-selected decoder. */

/* Caller-owned decoder handle and negotiated output format. */
typedef struct
{
    void *impl;
    uint8_t channels;
    uint32_t sample_rate;
} aac_decoder_t;

/* Initialize the decoder from MPEG-4 AudioSpecificConfig bytes. */
int aac_decoder_init(aac_decoder_t *decoder,
                     const uint8_t *config,
                     size_t config_len,
                     uint8_t channels,
                     uint32_t sample_rate);
/* Decode one AAC access unit into interleaved signed 16-bit PCM samples. */
int aac_decoder_decode(aac_decoder_t *decoder,
                       const uint8_t *input,
                       size_t input_len,
                       int16_t *output,
                       size_t output_capacity_samples,
                       size_t *output_samples);
/* Release the decoder implementation and reset its state. */
void aac_decoder_close(aac_decoder_t *decoder);

#endif
