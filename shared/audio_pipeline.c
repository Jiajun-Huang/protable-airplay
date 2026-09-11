#include "audio_pipeline.h"
#include "alac_decoder.h"
#include "crypto.h"
#include "log.h"
#include "ntp_sync.h"
#include "os.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    // Bound temporary decrypted data by the configured RTP datagram capacity.
    uint8_t decrypted_buffer[RTP_BUFFER_SIZE];
    const uint8_t *decode_input = payload;

    if (pipeline->session.has_encryption && payload_len >= 16)
    {
        // AirPlay encrypts only the full 16-byte prefix; any trailing bytes remain plaintext.
        size_t encrypted_len = (payload_len / 16) * 16;

        if (payload_len > sizeof(decrypted_buffer))
        {
            LOG_ERROR("pipeline", "Encrypted payload too large: %zu > %zu\n",
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
            LOG_ERROR("pipeline", "AES decryption failed\n");
            return -1;
        }
    }

    // Decode ALAC
    int result = -1;
    if (pipeline->alac_decoder.frame_length > 0)
    {
        result = alac_decoder_decode_frame(&pipeline->alac_decoder,
                                           decode_input, payload_len,
                                           output, output_samples,
                                           MAX_AUDIO_BUFFER_SAMPLES);
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
    if (!pipeline->session.channels || payload_len % (2 * pipeline->session.channels) != 0)
        return -1;

    uint8_t decrypted_buffer[RTP_BUFFER_SIZE];
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
static void audio_pipeline_decode_packet(const rtp_packet_t *packet, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    if (!pipeline)
    {
        static int logged_null = 0;
        if (!logged_null++)
            LOG_ERROR("pipeline", "RTP audio callback received NULL pipeline!\n");
        return;
    }

    if (pipeline->state != AUDIO_PIPELINE_PLAYING)
    {
        static int logged_state = 0;
        if (!logged_state++)
            LOG_WARN("pipeline", "RTP audio callback but pipeline not PLAYING (state=%d)\n", pipeline->state);
        return;
    }
    if (packet->header.payload_type != pipeline->session.payload_type)
        return;

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
            // LOG_DEBUG("pipeline", "Lost %u packets (seq jump %u -> %u)\n",
            //        lost, pipeline->last_sequence, packet->header.sequence);
        }
    }

    pipeline->last_sequence = packet->header.sequence;
    pipeline->packets_received++;

    // Decode audio packet
    int16_t *decoded_samples = pipeline->audio_buffer;
    size_t decoded_count = 0;

    if (pipeline->session.codec == SDP_CODEC_ALAC)
    {
        if (audio_pipeline_decode_alac(pipeline, packet->payload, packet->payload_len,
                                       decoded_samples, &decoded_count) < 0)
        {
            ++pipeline->decode_errors;
            if (pipeline->decode_errors <= 3 || pipeline->decode_errors % 100 == 0)
                LOG_ERROR("audio", "ALAC rejected: seq=%u bytes=%zu encrypted=%d errors=%u\n",
                        packet->header.sequence, packet->payload_len,
                        pipeline->session.has_encryption, pipeline->decode_errors);
            return;
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
        int peak = 0;
        for (size_t i = 0; i < decoded_count; ++i)
        {
            int value = decoded_samples[i];
            if (value < 0)
                value = -value;
            if (value > peak)
                peak = value;
        }
        if (peak)
            ++pipeline->nonzero_packets;
        if (pipeline->decoded_packets++ == 0 || (peak && pipeline->nonzero_packets == 1))
        {
            LOG_DEBUG("audio", "First decoded packet: seq=%u pt=%u bytes=%zu samples=%zu channels=%u peak=%d encrypted=%d\n",
                   packet->header.sequence, packet->header.payload_type, packet->payload_len,
                   decoded_count, pipeline->session.channels, peak, pipeline->session.has_encryption);
        }
        for (size_t i = 0; i < decoded_count; ++i)
            decoded_samples[i] = (int16_t)(decoded_samples[i] * pipeline->volume_linear);
        pipeline->on_audio_data(decoded_samples, decoded_count, pipeline->user_data);
    }

    // Debug output every 1000 packets
    if (pipeline->packets_received % 1000 == 0)
    {
        LOG_DEBUG("pipeline", "Played %u packets, gaps %u (%.2f%%), nonzero=%u queued=%u late=%u overflow=%u\n",
               pipeline->packets_received, pipeline->packets_lost,
               (pipeline->packets_lost * 100.0f) / pipeline->packets_received,
               pipeline->nonzero_packets, pipeline->playout.count,
               pipeline->late_packets, pipeline->queue_overflows);
    }
}

static void audio_pipeline_on_rtp_audio(const rtp_packet_t *packet, void *user_data)
{
    audio_pipeline_t *pipeline = user_data;
    if (pipeline->state != AUDIO_PIPELINE_PLAYING ||
        packet->header.payload_type != pipeline->session.payload_type)
        return;
    int result = playout_push(&pipeline->playout, packet);
    if (result < 0 && ++pipeline->queue_overflows <= 3)
                LOG_WARN("audio", "Playout capacity exceeded: queued=%u bytes=%zu\n",
                pipeline->playout.count, packet->payload_len);
    if (result > 0 && !pipeline->first_arrival_us)
    {
        pipeline->first_arrival_us = os_time_us();
        pipeline->first_timestamp = packet->header.timestamp;
        LOG_DEBUG("audio", "Buffering first RTP packet: seq=%u timestamp=%u bytes=%zu\n",
               packet->header.sequence, packet->header.timestamp, packet->payload_len);
    }
}

/**
 * @brief RTP control callback - receives RTCP packets
 */
static void audio_pipeline_on_rtp_control(const uint8_t *data, size_t len, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    uint32_t old_latency = pipeline->ntp_sync.latency_frames;
    int had_anchor = pipeline->ntp_sync.anchor_valid;
    uint32_t rate = pipeline->session.sample_rate ? pipeline->session.sample_rate : AIRPLAY_DEFAULT_SAMPLE_RATE;
    if (ntp_sync_control(&pipeline->ntp_sync, data, len, rate) == 0 &&
        (!had_anchor || old_latency != pipeline->ntp_sync.latency_frames))
        LOG_DEBUG("sync", "RTP anchor=%u sender latency=%u frames (%.1f ms), clock_ready=%d\n",
               pipeline->ntp_sync.rtp_base, pipeline->ntp_sync.latency_frames,
               pipeline->ntp_sync.latency_frames * 1000.0 / rate, pipeline->ntp_sync.synchronized);
}

/**
 * @brief RTP timing callback - receives timing/sync packets
 */
static void audio_pipeline_on_rtp_timing(const uint8_t *data, size_t len, const net_addr_t *peer, void *user_data)
{
    audio_pipeline_t *pipeline = (audio_pipeline_t *)user_data;
    if (!pipeline || !pipeline->ntp_sync_initialized)
        return;

    uint8_t reply[32];
    if (ntp_sync_reply(data, len, reply) == 0)
    {
        net_udp_send(&pipeline->rtp.timing_socket, reply, sizeof(reply), peer);
        return;
    }
    if (peer->port != pipeline->timing_peer.port)
        return;
    int had_clock = pipeline->ntp_sync.synchronized;
    if (ntp_sync_process_packet(&pipeline->ntp_sync, data, len) == 0 && !had_clock)
        LOG_DEBUG("sync", "Sender clock ready: RTT=%lld us offset=%lld us\n",
               (long long)pipeline->ntp_sync.rtt_us, (long long)pipeline->ntp_sync.clock_offset_us);
}

int audio_pipeline_create(audio_pipeline_t *pipeline, const audio_pipeline_config_t *config)
{
    if (!pipeline || !config)
        return -1;

    // Initialize pipeline struct (user-allocated memory)
    memset(pipeline, 0, sizeof(*pipeline));

    pipeline->on_audio_data = config->on_audio_data;
    pipeline->user_data = config->user_data;
    pipeline->output_delay_frames = config->output_delay_frames;
    pipeline->state = AUDIO_PIPELINE_STOPPED;
    pipeline->volume_db = 0.0f;
    pipeline->volume_linear = 1.0f;

    // Initialize NTP sync
    if (ntp_sync_init(&pipeline->ntp_sync) == 0)
        pipeline->ntp_sync_initialized = 1;
    else
    {
        LOG_WARN("pipeline", "Failed to create NTP sync\n");
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
        LOG_ERROR("pipeline", "Failed to create RTP receiver\n");
        if (pipeline->ntp_sync_initialized)
        {
            ntp_sync_deinit(&pipeline->ntp_sync);
            pipeline->ntp_sync_initialized = 0;
        }
        return -1;
    }

    LOG_INFO("pipeline", "Created audio pipeline (ports: %u/%u/%u)\n",
           config->audio_port, config->control_port, config->timing_port);

    return 0;
}

void audio_pipeline_set_transport(audio_pipeline_t *pipeline, const net_addr_t *peer)
{
    pipeline->timing_peer = *peer;
    memcpy(pipeline->rtp.peer_ip, peer->ip, sizeof(peer->ip));
    ntp_sync_init(&pipeline->ntp_sync);
}

void audio_pipeline_set_start(audio_pipeline_t *pipeline, uint32_t timestamp, int exclusive)
{
    playout_set_floor(&pipeline->playout, timestamp, exclusive);
    pipeline->first_arrival_us = 0;
}

int audio_pipeline_configure(audio_pipeline_t *pipeline, const sdp_session_t *session)
{
    if (!pipeline || !session || session->sample_rate == 0 ||
        session->channels == 0 || session->channels > 2 || session->bits_per_sample != 16 ||
        !session->frames_per_packet || session->frames_per_packet > ALAC_MAX_SAMPLES_PER_FRAME ||
        (size_t)session->frames_per_packet * session->channels > MAX_AUDIO_BUFFER_SAMPLES)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    alac_decoder_close(&impl->alac_decoder);
    memset(&impl->alac_decoder, 0, sizeof(impl->alac_decoder));
    memset(&impl->aes_context, 0, sizeof(impl->aes_context));
    impl->session = *session;
    impl->configured = 0;
    impl->state = AUDIO_PIPELINE_STOPPED;

    LOG_INFO("pipeline", "Configured: codec=%d, %uHz, %u-ch, %u-bit, %u frames/pkt\n",
           session->codec, session->sample_rate, session->channels,
           session->bits_per_sample, session->frames_per_packet);

    if (session->codec == SDP_CODEC_UNKNOWN)
    {
        // Compatibility fallback: some senders omit/alter rtpmap while still sending ALAC payloads.
        // Prefer continuing with ALAC decode over hard fail to avoid silent sessions.
        impl->session.codec = SDP_CODEC_ALAC;
        LOG_WARN("pipeline", "Unknown codec in SDP; falling back to ALAC\n");
    }

    if (impl->session.codec == SDP_CODEC_AAC)
    {
        LOG_WARN("pipeline", "AAC/AAC-ELD session received, but AAC decode is not implemented yet\n");
        return -1;
    }

    // Create ALAC decoder if needed
    if (impl->session.codec == SDP_CODEC_ALAC && impl->alac_decoder.frame_length == 0)
    {
                LOG_DEBUG("pipeline", "ALAC SDP: frames_per_packet=%u bit_depth=%u channels=%u rate=%u fmtp_count=%zu\n",
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
            LOG_ERROR("pipeline", "Failed to initialize ALAC decoder\n");
            return -1;
        }
    }

    // Initialize AES decryption if encrypted.
    // Sequence is important:
    // 1) RSA-decrypt per-session AES key from SDP rsaaeskey
    // 2) bind decrypted key with SDP aesiv in crypto_aes_init
    // If RSA key unwrap fails, abort configure to avoid decoding garbage audio.
    if (session->has_encryption)
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
                LOG_INFO("pipeline", "AES key decrypted via RSA\n");
            }
            else
            {
                LOG_ERROR("pipeline", "RSA AES-key decrypt failed\n");
            }
        }

        if (!have_key)
            return -1;

        if (have_key && crypto_aes_init(&impl->aes_context, aes_key, session->aes_iv) == 0)
        {
            LOG_INFO("pipeline", "AES-128-CBC decryption enabled\n");
        }
        else
        {
            LOG_ERROR("pipeline", "Failed to initialize AES decryption\n");
            return -1;
        }
    }

    impl->configured = 1;
    impl->state = AUDIO_PIPELINE_READY;

    return 0;
}

int audio_pipeline_start(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    if (!impl->configured)
    {
        LOG_ERROR("pipeline", "Cannot start - not configured\n");
        return -1;
    }

    impl->state = AUDIO_PIPELINE_PLAYING;
    impl->packets_received = 0;
    impl->packets_lost = 0;
    impl->decode_errors = 0;
    impl->decoded_packets = 0;
    impl->nonzero_packets = impl->queue_overflows = impl->late_packets = 0;

    LOG_INFO("pipeline", "Started playback\n");

    return 0;
}

static int audio_pipeline_flush(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    playout_reset(&impl->playout);
    impl->first_arrival_us = 0;
    impl->fallback_logged = 0;
    impl->ntp_sync.anchor_valid = 0;

    // Reset packet tracking
    impl->packets_received = 0;
    impl->packets_lost = 0;

    LOG_DEBUG("pipeline", "Flushed audio buffers\n");

    return 0;
}

int audio_pipeline_stop(audio_pipeline_t *pipeline)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    impl->state = AUDIO_PIPELINE_STOPPED;

    audio_pipeline_flush(pipeline);

    LOG_INFO("pipeline", "Stopped playback\n");

    return 0;
}

int audio_pipeline_poll(audio_pipeline_t *pipeline, int timeout_ms)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;

    uint8_t request[32];
    if (impl->timing_peer.port && ntp_sync_request(&impl->ntp_sync, request))
        net_udp_send(&impl->rtp.timing_socket, request, sizeof(request), &impl->timing_peer);
    if (impl->state == AUDIO_PIPELINE_PLAYING && (timeout_ms < 0 || timeout_ms > 2))
        timeout_ms = 2;
    if (rtp_receiver_poll(&impl->rtp, timeout_ms) != 0)
        return -1;
    for (unsigned batch = 0; batch < 16 && impl->state == AUDIO_PIPELINE_PLAYING; ++batch)
    {
        playout_packet_t *queued = playout_peek(&impl->playout);
        if (!queued)
            break;
        uint64_t now = os_time_us(), deadline;
        if (ntp_sync_deadline(&impl->ntp_sync, queued->header.timestamp,
                              impl->session.sample_rate, &deadline) != 0)
        {
            /* Use arrival time to pace playback when sender clock synchronization is unavailable. */
            if (impl->timing_peer.port && now - impl->first_arrival_us < 3000000)
                break;
            if (!impl->fallback_logged)
            {
                LOG_WARN("sync", "No sender clock/anchor; using relative 2 s buffering\n");
                impl->fallback_logged = 1;
            }
            deadline = impl->first_arrival_us + 2000000 +
                       (int64_t)(int32_t)(queued->header.timestamp - impl->first_timestamp) * 1000000 /
                           impl->session.sample_rate;
        }
        /* Bound native output buffering even when the sender transmits seconds ahead. */
        if ((int64_t)(deadline - now) > 40000)
            break;
        int delay = impl->output_delay_frames ? impl->output_delay_frames(impl->user_data) : 0;
        if (delay < 0)
            return -1;
        int64_t early = (int64_t)(deadline - now) - (int64_t)delay * 1000000 / impl->session.sample_rate;
        if (early > 5000)
            break;
        if (early < -100000)
        {
            ++impl->late_packets;
        }
        else
        {
            rtp_packet_t packet = {queued->header, queued->payload, queued->length};
            audio_pipeline_decode_packet(&packet, impl);
        }
        playout_pop(&impl->playout, queued);
    }
    return 0;
}

int audio_pipeline_set_volume(audio_pipeline_t *pipeline, float volume_db)
{
    if (!pipeline)
        return -1;

    audio_pipeline_t *impl = (audio_pipeline_t *)pipeline;
    if (!isfinite(volume_db))
        return -1;
    if (volume_db > 0.0f)
        volume_db = 0.0f;
    if (volume_db < -144.0f)
        volume_db = -144.0f;
    impl->volume_db = volume_db;
    impl->volume_linear = volume_db <= -120.0f ? 0.0f : powf(10.0f, volume_db / 20.0f);

    LOG_INFO("pipeline", "Volume set to %.3f dB\n", volume_db);

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

    if (impl->ntp_sync_initialized)
    {
        ntp_sync_deinit(&impl->ntp_sync);
        impl->ntp_sync_initialized = 0;
    }

    LOG_INFO("pipeline", "Closed audio pipeline\n");

    // Note: User is responsible for freeing the pipeline structure
}
