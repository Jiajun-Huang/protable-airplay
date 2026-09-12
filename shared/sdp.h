#ifndef SDP_H
#define SDP_H

#include <stdint.h>
#include <stddef.h>
#include "airplay_config.h"

/**
 * @brief SDP (Session Description Protocol) parser for AirPlay ANNOUNCE
 * Parses SDP payload from RTSP ANNOUNCE to extract codec parameters and encryption material.
 */

/**
 * @brief Audio codec types
 */
typedef enum
{
    SDP_CODEC_UNKNOWN = 0,
    SDP_CODEC_ALAC, // Apple Lossless
    SDP_CODEC_AAC,  // Advanced Audio Coding
    SDP_CODEC_PCM,  // Linear PCM
} sdp_codec_t;

/**
 * @brief Parsed SDP session information
 */
typedef struct
{
    sdp_codec_t codec;
    uint32_t sample_rate;       // Hz (e.g., 44100)
    uint16_t channels;          // 1=mono, 2=stereo
    uint16_t bits_per_sample;   // 16, 24, 32
    uint32_t frames_per_packet; // Frames in each packet

    // ALAC-specific
    uint32_t alac_fmtp[12]; // Parsed fmtp parameters for ALAC
    size_t alac_fmtp_count; // Number of valid fmtp parameters

    // MPEG-4 AAC-specific
    uint8_t aac_config[64]; // AudioSpecificConfig from fmtp=config
    size_t aac_config_len;
    uint8_t aac_size_length;
    uint8_t aac_index_length;
    uint8_t aac_index_delta_length;

    // Encryption
    uint8_t aes_key_encrypted[512]; // RSA-encrypted AES key from SDP (base64 or binary)
    size_t aes_key_encrypted_len;   // Length of encrypted AES key
    uint8_t aes_iv[16];             // AES-128 IV
    int has_encryption;

    // RTP
    uint8_t payload_type; // RTP payload type (usually 96)
    uint16_t stream_type; // 0 = RAOP, 96 = AirPlay 2 realtime, 103 = buffered
    uint8_t audio_key[32];
    uint32_t latency_frames;

} sdp_session_t;

/**
 * @brief Parse SDP from ANNOUNCE body
 * Extracts codec parameters and encryption material.
 *
 * @param sdp_data SDP text data
 * @param sdp_len length of SDP data
 * @param session output parsed session info
 * @param scratch scratch buffer for null-terminated copy
 * @param scratch_len size of scratch buffer
 * @return 0 on success, negative on error
 */
int sdp_parse(const uint8_t *sdp_data,
              size_t sdp_len,
              sdp_session_t *session,
              char *scratch,
              size_t scratch_len);

/**
 * @brief Decode base64 string
 * Helper for parsing base64-encoded keys/config.
 *
 * @param input base64 string
 * @param output output buffer
 * @param output_size size of output buffer
 * @return bytes written, or negative on error
 */
int sdp_base64_decode(const char *input, uint8_t *output, size_t output_size);

#endif // SDP_H
