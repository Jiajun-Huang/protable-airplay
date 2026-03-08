#include "alac_decoder.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define alac_create shairport_alac_create
#define alac_decode_frame shairport_alac_decode_frame
#define alac_allocate_buffers shairport_alac_allocate_buffers
#define alac_free shairport_alac_free
#include "../../shairport-sync/alac.h"
#undef alac_create
#undef alac_decode_frame
#undef alac_allocate_buffers
#undef alac_free

int alac_decoder_init(alac_decoder_t *decoder,
                      const uint32_t *fmtp, size_t fmtp_count,
                      uint32_t frames_per_packet,
                      uint8_t bit_depth,
                      uint8_t channels,
                      uint32_t sample_rate)
{
    if (!decoder)
        return -1;

    memset(decoder, 0, sizeof(*decoder));

    decoder->frame_length = frames_per_packet ? frames_per_packet : 352;
    decoder->bit_depth = bit_depth ? bit_depth : 16;
    decoder->channels = channels ? channels : 2;
    decoder->sample_rate = sample_rate ? sample_rate : 44100;

    if (fmtp && fmtp_count > 0)
    {
        decoder->fmtp_count = (fmtp_count > 12) ? 12 : fmtp_count;
        for (size_t i = 0; i < decoder->fmtp_count; i++)
            decoder->fmtp[i] = fmtp[i];
    }

    alac_file *alac = shairport_alac_create(decoder->bit_depth, decoder->channels);
    if (!alac)
        return -1;

    alac->setinfo_max_samples_per_frame = decoder->frame_length;
    alac->setinfo_7a = (decoder->fmtp_count > 2) ? (uint8_t)decoder->fmtp[2] : 0;
    alac->setinfo_sample_size = decoder->bit_depth;
    alac->setinfo_rice_historymult = (decoder->fmtp_count > 4) ? (uint8_t)decoder->fmtp[4] : 40;
    alac->setinfo_rice_initialhistory = (decoder->fmtp_count > 5) ? (uint8_t)decoder->fmtp[5] : 10;
    alac->setinfo_rice_kmodifier = (decoder->fmtp_count > 6) ? (uint8_t)decoder->fmtp[6] : 14;
    alac->setinfo_7f = (decoder->fmtp_count > 7) ? (uint8_t)decoder->fmtp[7] : 2;
    alac->setinfo_80 = (decoder->fmtp_count > 8) ? (uint16_t)decoder->fmtp[8] : 255;
    alac->setinfo_82 = (decoder->fmtp_count > 9) ? decoder->fmtp[9] : 0;
    alac->setinfo_86 = (decoder->fmtp_count > 10) ? decoder->fmtp[10] : 0;
    alac->setinfo_8a_rate = decoder->sample_rate;
    shairport_alac_allocate_buffers(alac);

    decoder->impl = (void *)alac;

    printf("[alac] Initialized real decoder: %u frames, %u-bit, %u channels, %u Hz\n",
           decoder->frame_length, decoder->bit_depth, decoder->channels, decoder->sample_rate);

    return 0;
}

int alac_decode_frame(alac_decoder_t *decoder,
                      const uint8_t *input, size_t input_len,
                      int16_t *output, size_t *output_samples, size_t max_output_samples)
{
    if (!decoder || !decoder->impl || !input || !output || !output_samples || input_len == 0)
        return -1;

    alac_file *alac = (alac_file *)decoder->impl;
    int output_bytes = 0;

    shairport_alac_decode_frame(alac, (unsigned char *)input, output, &output_bytes);

    if (output_bytes <= 0)
        return -1;

    size_t samples = (size_t)output_bytes / sizeof(int16_t);
    if (samples > max_output_samples)
        samples = max_output_samples;

    *output_samples = samples;
    return 0;
}

void alac_decoder_close(alac_decoder_t *decoder)
{
    if (!decoder || !decoder->impl)
        return;

    shairport_alac_free((alac_file *)decoder->impl);
    decoder->impl = NULL;
}