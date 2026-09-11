#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include "rtp.h"
#include "sdp.h"
#include "alac_decoder.h"
#include "aac_decoder.h"
#include "crypto.h"
#include "ntp_sync.h"
#include "playout.h"

/**
 * @brief Audio pipeline state
 */
typedef enum
{
    AUDIO_PIPELINE_STOPPED = 0,
    AUDIO_PIPELINE_READY,
    AUDIO_PIPELINE_PLAYING,
} audio_pipeline_state_t;

/**
 * @brief Audio pipeline - manages flow from RTP → decode → playback
 * Coordinates RTP receiver, audio decoder, and audio output.
 */

// Transparent structure for user-managed memory allocation
typedef struct
{
    rtp_receiver_t rtp;
    void (*on_audio_data)(const int16_t *samples, size_t sample_count, void *user_data);
    void *user_data;
    int (*output_delay_frames)(void *user_data);

    sdp_session_t session;
    int state; // audio_pipeline_state_t
    float volume_db;
    float volume_linear;

    // Decoder and crypto (user-managed memory)
    alac_decoder_t alac_decoder;      // Embedded decoder
    aac_decoder_t aac_decoder;        // Embedded decoder
    crypto_aes_context_t aes_context; // Embedded AES context
    ntp_sync_t ntp_sync;              // Embedded NTP sync state
    int ntp_sync_initialized;
    playout_t playout;
    net_addr_t timing_peer;
    uint64_t first_arrival_us;
    uint32_t first_timestamp, queue_overflows, late_packets, nonzero_packets;
    int fallback_logged;

    // Audio buffer for decoded samples
    int16_t audio_buffer[MAX_AUDIO_BUFFER_SAMPLES];

    // RTP packet tracking
    uint16_t last_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t decode_errors;
    uint32_t decoded_packets;

    int configured;
} audio_pipeline_t;

/**
 * @brief Audio pipeline configuration
 */
typedef struct
{
    // RTP ports
    uint16_t audio_port;
    uint16_t control_port;
    uint16_t timing_port;

    // Callbacks for decoded audio
    void (*on_audio_data)(const int16_t *samples, size_t sample_count, void *user_data);
    void *user_data;
    int (*output_delay_frames)(void *user_data);
} audio_pipeline_config_t;

/**
 * @brief Create and initialize audio pipeline
 * Memory for the pipeline struct must be allocated by the caller.
 *
 * @param pipeline pre-allocated pipeline struct (user-managed memory)
 * @param config pipeline configuration
 * @return 0 on success, -1 on error
 */
int audio_pipeline_create(audio_pipeline_t *pipeline, const audio_pipeline_config_t *config);

/**
 * @brief Configure pipeline with SDP session info
 * Must be called after ANNOUNCE before audio can be received.
 *
 * @param pipeline audio pipeline
 * @param session SDP session info from ANNOUNCE
 * @return 0 on success, negative on error
 */
int audio_pipeline_configure(audio_pipeline_t *pipeline, const sdp_session_t *session);
void audio_pipeline_set_transport(audio_pipeline_t *pipeline, const net_addr_t *timing_peer);
void audio_pipeline_set_start(audio_pipeline_t *pipeline, uint32_t timestamp, int exclusive);

/**
 * @brief Start audio playback
 * Begins receiving and decoding RTP packets.
 *
 * @param pipeline audio pipeline
 * @return 0 on success, negative on error
 */
int audio_pipeline_start(audio_pipeline_t *pipeline);

/**
 * @brief Stop audio playback
 * Clears buffered packets and their playback timeline. Sockets remain open for polling.
 *
 * @param pipeline audio pipeline
 * @return 0 on success, negative on error
 */
int audio_pipeline_stop(audio_pipeline_t *pipeline);

/**
 * @brief Poll pipeline for audio activity
 * Receives RTP packets, decodes audio, and sends to output callback.
 *
 * @param pipeline audio pipeline
 * @param timeout_ms milliseconds to wait
 * @return 0 on success, negative on error
 */
int audio_pipeline_poll(audio_pipeline_t *pipeline, int timeout_ms);

/**
 * @brief Set audio volume
 * @param pipeline audio pipeline
 * @param volume_db volume in dB (-144.0 to 0.0)
 * @return 0 on success, negative on error
 */
int audio_pipeline_set_volume(audio_pipeline_t *pipeline, float volume_db);

/**
 * @brief Close audio pipeline
 * Stops playback and releases all resources.
 *
 * @param pipeline audio pipeline
 */
void audio_pipeline_close(audio_pipeline_t *pipeline);

#endif // AUDIO_PIPELINE_H
