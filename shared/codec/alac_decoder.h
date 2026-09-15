

#ifndef ALAC_DECODER_H
#define ALAC_DECODER_H

#include <stddef.h>
#include <stdint.h>

/* Safe ALAC decoder adapter used by the audio pipeline. It validates negotiated
 * limits and presents sample counts instead of the low-level byte-oriented API. */

/**
 * @brief ALAC (Apple Lossless Audio Codec) decoder
 * Decodes ALAC-compressed audio to PCM samples.
 * User manages memory - decoder instance and buffers are provided by caller.
 */

typedef struct
{
    // Configuration from session/fmtp
    uint32_t frame_length;
    uint8_t bit_depth;
    uint8_t channels;
    uint32_t sample_rate;

    uint32_t fmtp[12];
    size_t fmtp_count;

    void *impl; // alac_file*

} alac_decoder_t;

/**
 * @brief Initialize ALAC decoder
 * User provides the decoder instance; this function initializes it.
 *
 * @param decoder pointer to user-allocated decoder
 * @param fmtp ALAC parameters parsed from SDP
 * @param fmtp_count number of fmtp parameters
 * @param frames_per_packet negotiated PCM frame count
 * @param bit_depth negotiated bits per sample
 * @param channels negotiated channel count
 * @param sample_rate negotiated sample rate in Hz
 * @return 0 on success, -1 on error
 */
int alac_decoder_init(alac_decoder_t *decoder,
                      const uint32_t *fmtp,
                      size_t fmtp_count,
                      uint32_t frames_per_packet,
                      uint8_t bit_depth,
                      uint8_t channels,
                      uint32_t sample_rate);

/**
 * @brief Decode ALAC frame to PCM
 * @param decoder ALAC decoder
 * @param input compressed ALAC data
 * @param input_len length of input
 * @param output PCM output buffer (16-bit signed samples)
 * @param output_samples number of samples decoded (frames * channels)
 * @param max_output_samples maximum samples output buffer can hold
 * @return 0 on success, negative on error
 */
int alac_decoder_decode_frame(alac_decoder_t *decoder,
                              const uint8_t *input,
                              size_t input_len,
                              int16_t *output,
                              size_t *output_samples,
                              size_t max_output_samples);

/* Release the low-level decoder context and reset negotiated state. */
/**
 * @brief alac_decoder_close.
 * @param decoder Parameter named decoder.
 */
void alac_decoder_close(alac_decoder_t *decoder);

#endif // ALAC_DECODER_H
