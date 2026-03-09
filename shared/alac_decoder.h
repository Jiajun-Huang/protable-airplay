#ifndef ALAC_DECODER_H
#define ALAC_DECODER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief ALAC (Apple Lossless Audio Codec) decoder
 * Decodes ALAC-compressed audio to PCM samples.
 * User manages memory - decoder instance and buffers are provided by caller.
 */

// Maximum frame size for ALAC (typical 352 samples per frame, stereo = 704)
#define ALAC_MAX_FRAME_SIZE 352

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

    // Working buffers (stereo)
    int32_t predict_error_ch0[ALAC_MAX_FRAME_SIZE * 2];
    int32_t predict_error_ch1[ALAC_MAX_FRAME_SIZE * 2];
    int32_t output_samples_ch0[ALAC_MAX_FRAME_SIZE * 2];
    int32_t output_samples_ch1[ALAC_MAX_FRAME_SIZE * 2];
} alac_decoder_t;

/**
 * @brief Initialize ALAC decoder
 * User provides the decoder instance; this function initializes it.
 *
 * @param decoder pointer to user-allocated decoder
 * @param magic_cookie ALAC configuration data from SDP fmtp
 * @param cookie_len length of magic cookie
 * @return 0 on success, -1 on error
 */
int alac_decoder_init(alac_decoder_t *decoder,
                      const uint32_t *fmtp, size_t fmtp_count,
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
                              const uint8_t *input, size_t input_len,
                              int16_t *output, size_t *output_samples, size_t max_output_samples);

void alac_decoder_close(alac_decoder_t *decoder);

#endif // ALAC_DECODER_H
