#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include "rtp.h"
#include "sdp.h"
#include "alac_decoder.h"
#include "crypto.h"
#include "ntp_sync.h"

#define MAX_AUDIO_BUFFER_SAMPLES (48000 * 2) // 1 second stereo @ 48kHz

/**
 * @brief Audio pipeline state
 */
typedef enum
{
    AUDIO_PIPELINE_STOPPED = 0,
    AUDIO_PIPELINE_READY,
    AUDIO_PIPELINE_PLAYING,
    AUDIO_PIPELINE_PAUSED,
    AUDIO_PIPELINE_FLUSHING,
} audio_pipeline_state_t;

/**
 * @brief Audio pipeline - manages flow from RTP → decode → playback
 * Coordinates RTP receiver, audio decoder, and audio output.
 */

// Transparent structure for user-managed memory allocation
typedef struct
{
    rtp_receiver_t rtp;
    uint16_t audio_port;
    uint16_t control_port;
    uint16_t timing_port;
    void (*on_audio_data)(const int16_t *samples, size_t sample_count, void *user_data);
    void (*on_state_change)(audio_pipeline_state_t state, void *user_data);
    void *user_data;

    sdp_session_t session;
    int state; // audio_pipeline_state_t
    float volume_db;

    // Decoder and crypto (user-managed memory)
    alac_decoder_t alac_decoder;      // Embedded decoder
    crypto_aes_context_t aes_context; // Embedded AES context
    ntp_sync_t *ntp_sync;             // Still a pointer until refactored

    // Audio buffer for decoded samples
    int16_t audio_buffer[MAX_AUDIO_BUFFER_SAMPLES];
    size_t audio_buffer_fill;

    // RTP packet tracking
    uint16_t last_sequence;
    uint32_t packets_received;
    uint32_t packets_lost;

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
    void (*on_state_change)(audio_pipeline_state_t state, void *user_data);
    void *user_data;
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

/**
 * @brief Start audio playback
 * Begins receiving and decoding RTP packets.
 *
 * @param pipeline audio pipeline
 * @return 0 on success, negative on error
 */
int audio_pipeline_start(audio_pipeline_t *pipeline);

/**
 * @brief Pause audio playback
 * @param pipeline audio pipeline
 * @return 0 on success, negative on error
 */
int audio_pipeline_pause(audio_pipeline_t *pipeline);

/**
 * @brief Flush audio buffers
 * Discards buffered audio and resets decoder.
 *
 * @param pipeline audio pipeline
 * @return 0 on success, negative on error
 */
int audio_pipeline_flush(audio_pipeline_t *pipeline);

/**
 * @brief Stop audio playback
 * Stops receiving RTP and releases resources.
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
 * @brief Get current pipeline state
 * @param pipeline audio pipeline
 * @return current state
 */
audio_pipeline_state_t audio_pipeline_get_state(const audio_pipeline_t *pipeline);

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
