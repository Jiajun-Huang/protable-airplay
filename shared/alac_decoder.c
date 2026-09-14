#include "alac_decoder.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "alac.h"

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

    decoder->frame_length = frames_per_packet ? frames_per_packet : AIRPLAY_DEFAULT_FRAMES_PER_PACKET;
    if (decoder->frame_length > ALAC_MAX_SAMPLES_PER_FRAME)
        return -1;

    decoder->bit_depth = bit_depth ? bit_depth : AIRPLAY_DEFAULT_BITS_PER_SAMPLE;
    if (decoder->bit_depth != 16 && decoder->bit_depth != 24)
        decoder->bit_depth = AIRPLAY_DEFAULT_BITS_PER_SAMPLE;

    decoder->channels = channels ? channels : AIRPLAY_DEFAULT_CHANNELS;
    if (decoder->channels == 0 || decoder->channels > 2)
        decoder->channels = AIRPLAY_DEFAULT_CHANNELS;

    decoder->sample_rate = sample_rate ? sample_rate : AIRPLAY_DEFAULT_SAMPLE_RATE;

    if (fmtp && fmtp_count > 0)
    {
        decoder->fmtp_count = (fmtp_count > 12) ? 12 : fmtp_count;
        for (size_t i = 0; i < decoder->fmtp_count; i++)
            decoder->fmtp[i] = fmtp[i];

        LOG_DEBUG("alac", "fmtp_count=%zu values:", decoder->fmtp_count);
        for (size_t i = 0; i < decoder->fmtp_count; i++)
            LOG_DEBUG("alac", " %u", decoder->fmtp[i]);
        LOG_DEBUG("alac", "\n");
    }

    alac_file *alac = alac_create(decoder->bit_depth, decoder->channels);
    if (!alac)
        return -1;

    // AirPlay ALAC fmtp layout:
    // [0]=frameLength [1]=compatibleVersion [2]=bitDepth [3]=pb [4]=mb [5]=kb
    // [6]=channels [7]=maxRun [8]=maxFrameBytes [9]=avgBitRate [10]=sampleRate
    alac->setinfo_max_samples_per_frame = decoder->frame_length;
    alac->setinfo_7a = (decoder->fmtp_count > 1) ? (uint8_t)decoder->fmtp[1] : 0;
    alac->setinfo_sample_size = decoder->bit_depth;
    alac->setinfo_rice_historymult = (decoder->fmtp_count > 3) ? (uint8_t)decoder->fmtp[3] : 40;
    alac->setinfo_rice_initialhistory = (decoder->fmtp_count > 4) ? (uint8_t)decoder->fmtp[4] : 10;
    alac->setinfo_rice_kmodifier = (decoder->fmtp_count > 5) ? (uint8_t)decoder->fmtp[5] : 14;
    alac->setinfo_7f = (decoder->fmtp_count > 6) ? (uint8_t)decoder->fmtp[6] : decoder->channels;
    alac->setinfo_80 = (decoder->fmtp_count > 7) ? (uint16_t)decoder->fmtp[7] : 255;
    alac->setinfo_82 = (decoder->fmtp_count > 8) ? decoder->fmtp[8] : 0;
    alac->setinfo_86 = (decoder->fmtp_count > 9) ? decoder->fmtp[9] : 0;
    alac->setinfo_8a_rate = decoder->sample_rate;
    alac_allocate_buffers(alac);

    decoder->impl = (void *)alac;

    LOG_INFO("alac", "Initialized real decoder: %u frames, %u-bit, %u channels, %u Hz\n",
             decoder->frame_length, decoder->bit_depth, decoder->channels, decoder->sample_rate);
    LOG_DEBUG("alac", "setinfo: max_frame=%u compat=%u sample_size=%u rice={%u,%u,%u} ch=%u maxRun=%u maxFrameBytes=%u avgBitRate=%u rate=%u\n",
              alac->setinfo_max_samples_per_frame,
              alac->setinfo_7a,
              alac->setinfo_sample_size,
              alac->setinfo_rice_historymult,
              alac->setinfo_rice_initialhistory,
              alac->setinfo_rice_kmodifier,
              alac->setinfo_7f,
              alac->setinfo_80,
              alac->setinfo_82,
              alac->setinfo_86,
              alac->setinfo_8a_rate);

    return 0;
}

int alac_decoder_decode_frame(alac_decoder_t *decoder,
                              const uint8_t *input, size_t input_len,
                              int16_t *output, size_t *output_samples, size_t max_output_samples)
{
    if (!decoder || !decoder->impl || !input || !output || !output_samples || input_len == 0)
        return -1;

    alac_file *alac = (alac_file *)decoder->impl;
    if (max_output_samples == 0)
        return -1;

    size_t max_output_bytes = max_output_samples * sizeof(int16_t);
    if (max_output_bytes > (size_t)INT_MAX)
        max_output_bytes = (size_t)INT_MAX;

    int output_bytes = (int)max_output_bytes;

    *output_samples = 0;
    alac_decode_frame(alac, input, input_len, output, &output_bytes);

    if (output_bytes <= 0)
        return -1;

    size_t samples = (size_t)output_bytes / sizeof(int16_t);
    if (samples > max_output_samples || samples % decoder->channels != 0)
        return -1;

    *output_samples = samples;
    return 0;
}

void alac_decoder_close(alac_decoder_t *decoder)
{
    if (!decoder || !decoder->impl)
        return;

    alac_free((alac_file *)decoder->impl);
    decoder->impl = NULL;
}
