#ifndef AAC_DECODER_H
#define AAC_DECODER_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    void *impl;
    uint8_t channels;
    uint32_t sample_rate;
} aac_decoder_t;

int aac_decoder_init(aac_decoder_t *decoder,
                     const uint8_t *config, size_t config_len,
                     uint8_t channels, uint32_t sample_rate);
int aac_decoder_decode(aac_decoder_t *decoder,
                       const uint8_t *input, size_t input_len,
                       int16_t *output, size_t output_capacity_samples,
                       size_t *output_samples);
void aac_decoder_close(aac_decoder_t *decoder);

#endif