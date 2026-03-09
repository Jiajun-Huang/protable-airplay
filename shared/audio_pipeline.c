#include "audio_pipeline.h"
#include "alac_decoder.h"
#include "crypto.h"
#include "ntp_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_AUDIO_BUFFER_SAMPLES (48000 * 2) // 1 second stereo @ 48kHz

// No longer needed - structure is now defined in header for user allocation
// typedef struct audio_pipeline_internal_t
// (moved to audio_pipeline.h as audio_pipeline_t)

/**
 * @brief Decode ALAC packet using real decoder
 */
static int audio_pipeline_decode_alac(audio_pipeline_t *pipeline,
                                      const uint8_t *payload, size_t payload_len,
                                      int16_t *output, size_t *output_samples)
{
    if (!pipeline || !payload || !output || !output_samples)
        return -1;

    // Decrypt if encryption is enabled
    // Use stack buffer for temporary decrypted data (max 4KB per packet)
    uint8_t decrypted_buffer[4096];
    const uint8_t *decode_input = payload;

    if (pipeline->session.has_encryption && payload_len >= 16)
    {
        // AirPlay encrypts only the full 16-byte prefix; any trailing bytes remain plaintext.
        size_t encrypted_len = (payload_len / 16) * 16;

        if (payload_len > sizeof(decrypted_buffer))
        {
            fprintf(stderr, "[pipeline] Encrypted payload too large: %zu > %zu\n",
                    payload_len, sizeof(decrypted_buffer));
            return -1;
        }

        if (crypto_aes_decrypt(&pipeline->aes_context, payload, decrypted_buffer, encrypted_len) == 0)
        {
            if (payload_len > encrypted_len)
            {
                memcpy(decrypted_buffer + encrypted_len,
                       payload + encrypted_len,
                       payload_len - encrypted_len);
            }
            decode_input = decrypted_buffer;
            // Decode the entire packet: decrypted prefix + plaintext tail.
        }
        else
        {
            fprintf(stderr, "[pipeline] AES decryption failed\n");
            return -1;
        }
    }

    // Decode ALAC
    int result = -1;
    if (pipeline->alac_decoder.frame_length > 0)
    {
        result = alac_decode_frame(&pipeline->alac_decoder,
                                   decode_input, payload_len,
                                   output, output_samples,
                                   MAX_AUDIO_BUFFER_SAMPLES);
    }
    else
    {
        // Fallback: generate silence
        size_t frame_count = pipeline->session.frames_per_packet;
        if (frame_count == 0)
            frame_count = 352;
        size_t sample_count = frame_count * pipeline->session.channels;

        if (sample_count > MAX_AUDIO_BUFFER_SAMPLES)
            sample_count = MAX_AUDIO_BUFFER_SAMPLES;

        memset(output, 0, sample_count * sizeof(int16_t));
        *output_samples = sample_count;
        result = 0;
    }

    return result;
}

/**
 * @brief Decode PCM (L16) RTP payload to native int16 samples
 */
static int audio_pipeline_decode_pcm(audio_pipeline_t *pipeline,
                                     const uint8_t *payload, size_t payload_len,
                                     int16_t *output, size_t *output_samples)
{
    if (!pipeline || !payload || !output || !output_samples)
        return -1;

    uint8_t decrypted_buffer[4096];
    const uint8_t *decode_input = payload;
    size_t decode_len = payload_len;

    if (pipeline->session.has_encryption && payload_len >= 16)
    {
        size_t encrypted_len = (payload_len / 16) * 16;
        if (payload_len > sizeof(decrypted_buffer))
            return -1;

        if (crypto_aes_decrypt(&pipeline->aes_context, payload, decrypted_buffer, encrypted_len) != 0)
            return -1;

        if (payload_len > encrypted_len)
        {
            memcpy(decrypted_buffer + encrypted_len,
                   payload + encrypted_len,
                   payload_len - encrypted_len);
        }

        decode_input = decrypted_buffer;
        decode_len = payload_len;
    }

    size_t sample_count = decode_len / 2;
    if (sample_count > MAX_AUDIO_BUFFER_SAMPLES)
        sample_count = MAX_AUDIO_BUFFER_SAMPLES;

    for (size_t i = 0; i < sample_count; i++)
    {
        // RTP L16 payload is network byte order (big-endian)
        uint16_t be = ((uint16_t)decode_input[i * 2] << 8) | (uint16_t)decode_input[i * 2 + 1];
        output[i] = (int16_t)be;
    }

    *output_samples = sample_count;
    return 0;
}

/**
 * @brief RTP audio callback - receives audio packets
 */
static void audio_pipeline_on_rtp_audio(const rtp_packet_t *packet, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    if (!pipeline)
    {
        static int logged_null = 0;
        if (!logged_null++)
            fprintf(stderr, "[pipeline] ERROR: RTP audio callback received NULL pipeline!\n");
        return;
    }

    if (pipeline->state != AUDIO_PIPELINE_PLAYING)
    {
        static int logged_state = 0;
        if (!logged_state++)
            fprintf(stderr, "[pipeline] WARNING: RTP audio callback but pipeline not PLAYING (state=%d)\n", pipeline->state);
        return;
    }

    // Track sequence numbers to detect loss
    if (pipeline->packets_received > 0)
    {
        uint16_t expected = pipeline->last_sequence + 1;
        if (packet->header.sequence != expected)
        {
            // IMPORTANT: only positive forward jumps are true loss.
            // Reordered or wrapped sequence numbers must not be counted as dropped packets.
            int16_t delta = (int16_t)(packet->header.sequence - expected);
            if (delta > 0)
                pipeline->packets_lost += (uint32_t)delta;
            // printf("[pipeline] Lost %u packets (seq jump %u -> %u)\n",
            //        lost, pipeline->last_sequence, packet->header.sequence);
        }
    }

    pipeline->last_sequence = packet->header.sequence;
    pipeline->packets_received++;

    // Decode audio packet
    int16_t decoded_samples[MAX_AUDIO_BUFFER_SAMPLES];
    size_t decoded_count = 0;

    if (pipeline->session.codec == SDP_CODEC_ALAC)
    {
        if (audio_pipeline_decode_alac(pipeline, packet->payload, packet->payload_len,
                                       decoded_samples, &decoded_count) < 0)
        {
            return;
        }

        // Some senders/decoder paths may yield mono-sized output for stereo ALAC sessions.
        // Expand in-place to interleaved stereo to keep stream timing correct.
        // ATTENTION: removing this fallback can reintroduce half-rate playback on some devices.
        if (pipeline->session.channels == 2 &&
            pipeline->session.frames_per_packet > 0 &&
            decoded_count == pipeline->session.frames_per_packet &&
            decoded_count * 2 <= MAX_AUDIO_BUFFER_SAMPLES)
        {
            for (size_t i = decoded_count; i-- > 0;)
            {
                int16_t s = decoded_samples[i];
                decoded_samples[i * 2] = s;
                decoded_samples[i * 2 + 1] = s;
            }
            decoded_count *= 2;

            static int mono_fix_logged = 0;
            if (!mono_fix_logged++)
            {
                printf("[pipeline] Expanded mono ALAC frame to stereo (%u -> %u samples)\n",
                       (unsigned)pipeline->session.frames_per_packet,
                       (unsigned)decoded_count);
            }
        }
    }
    else if (pipeline->session.codec == SDP_CODEC_PCM)
    {
        if (audio_pipeline_decode_pcm(pipeline, packet->payload, packet->payload_len,
                                      decoded_samples, &decoded_count) < 0)
        {
            return;
        }
    }
    else
    {
        // Other codecs not implemented
        return;
    }

    // Send to output callback
    if (decoded_count > 0 && pipeline->on_audio_data)
    {
        pipeline->on_audio_data(decoded_samples, decoded_count, pipeline->user_data);
    }

    // Debug output every 1000 packets
    if (pipeline->packets_received % 1000 == 0)
    {
        printf("[pipeline] Received %u packets, lost %u (%.2f%%)\n",
               pipeline->packets_received, pipeline->packets_lost,
               (pipeline->packets_lost * 100.0f) / pipeline->packets_received);
    }
}

/**
 * @brief RTP control callback - receives RTCP packets
 */
static void audio_pipeline_on_rtp_control(const uint8_t *data, size_t len, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    (void)pipeline;
    (void)data;
    (void)len;
    // RTCP handling could be implemented here
}

/**
 * @brief RTP timing callback - receives timing/sync packets
 */
static void audio_pipeline_on_rtp_timing(const uint8_t *data, size_t len, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    if (!pipeline || !pipeline->ntp_sync)
        return;

    // Process NTP timing packet for clock synchronization
    ntp_sync_process_packet(pipeline->ntp_sync, data, len);
}

int audio_pipeline_create(audio_pipeline_t *pipeline, const audio_pipeline_config_t *config)
{
    if (!pipeline || !config)
        return -1;

    // Initialize pipeline struct (user-allocated memory)
    memset(pipeline, 0, sizeof(*pipeline));

    pipeline->audio_port = config->audio_port;
    pipeline->control_port = config->control_port;
    pipeline->timing_port = config->timing_port;
    pipeline->on_audio_data = config->on_audio_data;
    pipeline->on_state_change = config->on_state_change;
    pipeline->user_data = config->user_data;
    pipeline->state = AUDIO_PIPELINE_STOPPED;
    pipeline->volume_db = 0.0f;

    // Create NTP sync
    pipeline->ntp_sync = ntp_sync_create();
    if (!pipeline->ntp_sync)
    {
        fprintf(stderr, "[pipeline] Warning: Failed to create NTP sync\n");
    }

    // Create RTP receiver
    rtp_receiver_config_t rtp_config = {0};
    rtp_config.audio_port = config->audio_port;
    rtp_config.control_port = config->control_port;
    rtp_config.timing_port = config->timing_port;
    rtp_config.audio_cb = audio_pipeline_on_rtp_audio;
    rtp_config.control_cb = audio_pipeline_on_rtp_control;
    rtp_config.timing_cb = audio_pipeline_on_rtp_timing;
    rtp_config.user_data = pipeline;

    if (rtp_receiver_create(&pipeline->rtp, &rtp_config) != 0)
    {
        fprintf(stderr, "[pipeline] Failed to create RTP receiver\n");
        if (pipeline->ntp_sync)
            ntp_sync_close(pipeline->ntp_sync);
        return -1;
    }

    printf("[pipeline] Created audio pipeline (ports: %u/%u/%u)\n",
           config->audio_port, config->control_port, config->timing_port);

    return 0;
}

int audio_pipeline_configure(audio_pipeline_t *pipeline, const sdp_session_t *session)
{
    if (!pipeline || !session)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    impl->session = *session;
    impl->configured = 1;

    printf("[pipeline] Configured: codec=%d, %uHz, %u-ch, %u-bit, %u frames/pkt\n",
           session->codec, session->sample_rate, session->channels,
           session->bits_per_sample, session->frames_per_packet);

    // Create ALAC decoder if needed
    if (session->codec == SDP_CODEC_ALAC && impl->alac_decoder.frame_length == 0)
    {
        printf("[pipeline] ALAC SDP: frames_per_packet=%u bit_depth=%u channels=%u rate=%u fmtp_count=%zu\n",
               session->frames_per_packet,
               session->bits_per_sample,
               session->channels,
               session->sample_rate,
               session->alac_fmtp_count);
        if (alac_decoder_init(&impl->alac_decoder,
                              session->alac_fmtp, session->alac_fmtp_count,
                              session->frames_per_packet,
                              (uint8_t)session->bits_per_sample,
                              (uint8_t)session->channels,
                              session->sample_rate) != 0)
        {
            fprintf(stderr, "[pipeline] Warning: Failed to initialize ALAC decoder\n");
        }
    }

    // Initialize AES decryption if encrypted.
    // Sequence is important:
    // 1) RSA-decrypt per-session AES key from SDP rsaaeskey
    // 2) bind decrypted key with SDP aesiv in crypto_aes_init
    // If RSA key unwrap fails, abort configure to avoid decoding garbage audio.
    if (session->has_encryption && impl->aes_context.key[0] == 0)
    {
        uint8_t aes_key[16];
        memset(aes_key, 0, sizeof(aes_key));

        int have_key = 0;
        if (session->aes_key_encrypted_len > 0)
        {
            if (crypto_rsa_decrypt_aes_key(session->aes_key_encrypted,
                                           session->aes_key_encrypted_len,
                                           aes_key) == 0)
            {
                have_key = 1;
                printf("[pipeline] AES key decrypted via RSA\n");
            }
            else
            {
                fprintf(stderr, "[pipeline] Error: RSA AES-key decrypt failed\n");
            }
        }

        if (!have_key)
            return -1;

        if (have_key && crypto_aes_init(&impl->aes_context, aes_key, session->aes_iv) == 0)
        {
            printf("[pipeline] AES-128-CBC decryption enabled\n");
        }
        else
        {
            fprintf(stderr, "[pipeline] Warning: Failed to initialize AES decryption\n");
        }
    }

    impl->state = AUDIO_PIPELINE_READY;
    if (impl->on_state_change)
        impl->on_state_change(impl->state, impl->user_data);

    return 0;
}

int audio_pipeline_start(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    if (!impl->configured)
    {
        fprintf(stderr, "[pipeline] Cannot start - not configured\n");
        return -1;
    }

    impl->state = AUDIO_PIPELINE_PLAYING;
    impl->packets_received = 0;
    impl->packets_lost = 0;

    printf("[pipeline] Started playback\n");

    if (impl->on_state_change)
        impl->on_state_change(impl->state, impl->user_data);

    return 0;
}

int audio_pipeline_pause(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    impl->state = AUDIO_PIPELINE_PAUSED;

    printf("[pipeline] Paused playback\n");

    if (impl->on_state_change)
        impl->on_state_change(impl->state, impl->user_data);

    return 0;
}

int audio_pipeline_flush(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    // Clear audio buffer
    impl->audio_buffer_fill = 0;

    // Reset packet tracking
    impl->packets_received = 0;
    impl->packets_lost = 0;

    printf("[pipeline] Flushed audio buffers\n");

    return 0;
}

int audio_pipeline_stop(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    impl->state = AUDIO_PIPELINE_STOPPED;

    audio_pipeline_flush(pipeline);

    printf("[pipeline] Stopped playback\n");

    if (impl->on_state_change)
        impl->on_state_change(impl->state, impl->user_data);

    return 0;
}

int audio_pipeline_poll(audio_pipeline_t *pipeline, int timeout_ms)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    // Poll RTP receiver
    return rtp_receiver_poll(&impl->rtp, timeout_ms);
}

audio_pipeline_state_t audio_pipeline_get_state(const audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return AUDIO_PIPELINE_STOPPED;

    const audio_pipeline_t *impl = (const audio_pipeline_t *)pipeline;
    return impl->state;
}

int audio_pipeline_set_volume(audio_pipeline_t *pipeline, float volume_db)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    impl->volume_db = volume_db;

    printf("[pipeline] Volume set to %.3f dB\n", volume_db);

    return 0;
}

void audio_pipeline_close(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    audio_pipeline_stop(pipeline);

    // Close embedded RTP receiver
    rtp_receiver_close(&impl->rtp);

    // ALAC decoder backend
    alac_decoder_close(&impl->alac_decoder);

    // ALAC decoder is embedded, no cleanup needed
    // AES context is embedded, no cleanup needed

    if (impl->ntp_sync)
        ntp_sync_close(impl->ntp_sync);

    printf("[pipeline] Closed audio pipeline\n");

    // Note: User is responsible for freeing the pipeline structure
}
