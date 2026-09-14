#include "aac_decoder.h"

#include <limits.h>
#include <string.h>

#if defined(AIRPLAY_ENABLE_FDK_AAC)
#include <aacdecoder_lib.h>
#endif

int aac_decoder_init(aac_decoder_t *decoder,
                     const uint8_t *config, size_t config_len,
                     uint8_t channels, uint32_t sample_rate)
{
    if (!decoder || !channels || channels > 2 || !sample_rate)
        return -1;

    memset(decoder, 0, sizeof(*decoder));
    decoder->channels = channels;
    decoder->sample_rate = sample_rate;

#if defined(AIRPLAY_ENABLE_FDK_AAC)
    HANDLE_AACDECODER handle = aacDecoder_Open(TT_MP4_RAW, 1);
    if (!handle)
        return -1;
    if (config && config_len)
    {
        UCHAR *config_buffers[1] = {(UCHAR *)config};
        UINT config_sizes[1] = {(UINT)config_len};
        if (aacDecoder_ConfigRaw(handle, config_buffers, config_sizes) != AAC_DEC_OK)
        {
            aacDecoder_Close(handle);
            return -1;
        }
    }
    decoder->impl = handle;
    return 0;
#else
    (void)config;
    (void)config_len;
    return -1;
#endif
}

int aac_decoder_decode(aac_decoder_t *decoder,
                       const uint8_t *input, size_t input_len,
                       int16_t *output, size_t output_capacity_samples,
                       size_t *output_samples)
{
    if (!decoder || !decoder->impl || !input || !input_len || !output ||
        !output_samples || output_capacity_samples > (size_t)INT_MAX)
        return -1;

    *output_samples = 0;
#if defined(AIRPLAY_ENABLE_FDK_AAC)
    HANDLE_AACDECODER handle = (HANDLE_AACDECODER)decoder->impl;
    UCHAR *buffers[1] = {(UCHAR *)input};
    UINT buffer_sizes[1] = {(UINT)input_len};
    UINT bytes_valid = (UINT)input_len;
    if (aacDecoder_Fill(handle, buffers, buffer_sizes, &bytes_valid) != AAC_DEC_OK)
        return -1;

    if (aacDecoder_DecodeFrame(handle, (INT_PCM *)output,
                               (INT)output_capacity_samples, 0) != AAC_DEC_OK)
        return -1;

    CStreamInfo *info = aacDecoder_GetStreamInfo(handle);
    if (!info || info->numChannels <= 0 || info->numChannels > 2 ||
        info->numChannels != decoder->channels || info->frameSize <= 0)
        return -1;
    *output_samples = (size_t)info->frameSize * (size_t)info->numChannels;
    if (*output_samples > output_capacity_samples)
    {
        *output_samples = 0;
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

void aac_decoder_close(aac_decoder_t *decoder)
{
    if (!decoder)
        return;
#if defined(AIRPLAY_ENABLE_FDK_AAC)
    if (decoder->impl)
        aacDecoder_Close((HANDLE_AACDECODER)decoder->impl);
#endif
    memset(decoder, 0, sizeof(*decoder));
}