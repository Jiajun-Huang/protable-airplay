#include "service/server.h"
#include "crypto/crypto_memory.h"
#include "util/network_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief server_fail.
 * @param server Parameter named server.
 */
static void server_fail(airplay_server_t *server)
{
    os_mutex_lock(&server->control_lock);
    server->result = -1;
    server->stopping = 1;
    os_mutex_unlock(&server->control_lock);
}

/**
 * @brief output_pcm.
 * @param samples Parameter named samples.
 * @param count Parameter named count.
 * @param arg Parameter named arg.
 */
static void output_pcm(const int16_t *samples, size_t count, void *arg)
{
    airplay_server_t *server = arg;
    if (server->audio && !airplay_server_is_stopping(server) &&
        audio_write(server->audio, samples, count) != (int)count)
        server_fail(server);
}

/**
 * @brief output_delay_frames.
 * @param arg Parameter named arg.
 * @return Function result.
 */
static int output_delay_frames(void *arg)
{
    airplay_server_t *server = arg;
    return server->audio ? audio_delay_frames(server->audio) : 0;
}

/**
 * @brief valid_config.
 * @param config Parameter named config.
 * @return Function result.
 */
static int valid_config(const airplay_config_t *config)
{
    uint32_t address;
    if (!config || !memchr(config->local_ip, 0, sizeof(config->local_ip)) ||
        !memchr(config->device_name, 0, sizeof(config->device_name)) ||
        config->device_name[0] == 0 || config->local_mac_hex[12] != 0 ||
        net_str_to_ipv4(config->local_ip, &address) != 0)
        return 0;
    for (size_t i = 0; i < 12; ++i)
        if (!isxdigit((unsigned char)config->local_mac_hex[i]))
            return 0;
    return 1;
}

int airplay_server_init(airplay_server_t *server, const airplay_config_t *config)
{
    if (!server || !valid_config(config))
        return -1;
    memset(server, 0, sizeof(*server));
    if (crypto_memory_init() != 0)
        return -1;
    if (os_mutex_init(&server->control_lock) != 0)
    {
        crypto_memory_deinit();
        return -1;
    }
    server->initialized = 1;
    server->config = *config;
    if (rtsp_server_create(&server->rtsp, AIRPLAY_RTSP_PORT) != 0)
        goto fail;
    server->rtsp_initialized = 1;
    if (rtsp_set_identity(&server->rtsp, config->local_ip, config->local_mac_hex) != 0)
        goto fail;
    strcpy(server->rtsp.device_name, config->device_name);

    audio_pipeline_config_t audio_config = {0};
    audio_config.audio_port = AIRPLAY_AUDIO_PORT;
    audio_config.control_port = AIRPLAY_CONTROL_PORT;
    audio_config.timing_port = AIRPLAY_TIMING_PORT;
    audio_config.on_audio_data = output_pcm;
    audio_config.output_delay_frames = output_delay_frames;
    audio_config.user_data = server;
    audio_config.local_ip = config->local_ip;
    if (audio_pipeline_create(&server->pipeline, &audio_config) != 0)
        goto fail;
    server->audio_initialized = 1;

    snprintf(server->deviceid_txt,
             sizeof(server->deviceid_txt),
             "deviceid=%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
             config->local_mac_hex,
             config->local_mac_hex + 2,
             config->local_mac_hex + 4,
             config->local_mac_hex + 6,
             config->local_mac_hex + 8,
             config->local_mac_hex + 10);
    const char *txt[] = {server->deviceid_txt, AIRPLAY_RAOP_TXT_ENTRIES};
    memcpy(server->raop_txt, txt, sizeof(txt));
    if (airplay_discovery_init(&server->discovery,
                               server->config.device_name,
                               server->config.local_mac_hex,
                               server->config.local_ip,
                               server->raop_txt,
                               sizeof(txt) / sizeof(txt[0])) != 0)
        goto fail;
    server->discovery_initialized = 1;
    return 0;
fail:
    airplay_server_deinit(server);
    return -1;
}

void airplay_server_stop(airplay_server_t *server)
{
    os_mutex_lock(&server->control_lock);
    server->stopping = 1;
    os_mutex_unlock(&server->control_lock);
}

int airplay_server_is_stopping(airplay_server_t *server)
{
    os_mutex_lock(&server->control_lock);
    int result = server->stopping;
    os_mutex_unlock(&server->control_lock);
    return result;
}

int airplay_server_result(airplay_server_t *server)
{
    os_mutex_lock(&server->control_lock);
    int result = server->result;
    os_mutex_unlock(&server->control_lock);
    return result;
}

/* Thread 1: mDNS discovery. Owns the UDP mDNS socket on port 5353 and
 * advertises the receiver's AirPlay service, whose RTSP endpoint is TCP
 * port AIRPLAY_RTSP_PORT. It does not handle RTSP control or audio packets. */
void airplay_mdns_main(void *arg)
{
    airplay_server_t *server = arg;
    while (!airplay_server_is_stopping(server))
        if (airplay_discovery_poll(&server->discovery, 100) != 0)
            server_fail(server);
}

/* Thread 2: RTSP control. Owns the TCP listener on AIRPLAY_RTSP_PORT,
 * accepts sender requests, and updates shared stream state such as the
 * session, transport, volume, and timestamp controls. It does not decode or
 * play audio. */
void airplay_rtsp_main(void *arg)
{
    airplay_server_t *server = arg;
    while (!airplay_server_is_stopping(server))
        if (rtsp_server_poll(&server->rtsp, 100) != 0)
            server_fail(server);
}

/**
 * @brief close_audio.
 * @param server Parameter named server.
 */
static void close_audio(airplay_server_t *server)
{
    audio_pipeline_stop(&server->pipeline);
    if (server->audio)
    {
        audio_close(server->audio);
        server->audio = NULL;
    }
}

typedef struct
{
    unsigned generation;
    unsigned flush_generation;
    unsigned transport_generation;
    float volume_db;
} audio_loop_state_t;

static void update_audio_transport(airplay_server_t *server,
                                   const rtsp_stream_state_t *stream,
                                   audio_loop_state_t *state)
{
    if (state->transport_generation == stream->generation)
        return;

    net_addr_t peer = stream->has_session ? stream->timing_peer : (net_addr_t){0};
    audio_pipeline_set_transport(&server->pipeline, &peer);
    state->transport_generation = stream->generation;
}

static int start_audio_session(airplay_server_t *server,
                               const rtsp_stream_state_t *stream,
                               audio_loop_state_t *state)
{
    if (server->audio && state->generation == stream->generation &&
        state->flush_generation == stream->flush_generation)
        return 0;

    close_audio(server);
    
    if (audio_pipeline_configure(&server->pipeline, &stream->session) != 0)
        return -1;

    if (audio_open(&server->audio,
                   stream->session.sample_rate,
                   (uint8_t)stream->session.channels,
                   16) != 0)
        return -1;

    if (audio_pipeline_start(&server->pipeline) != 0)
    {
        close_audio(server);
        return -1;
    }

    state->generation = stream->generation;
    state->flush_generation = stream->flush_generation;
    state->volume_db = stream->volume_db;
    audio_pipeline_set_volume(&server->pipeline, state->volume_db);
    if (stream->has_timestamp_floor)
        audio_pipeline_set_start(
            &server->pipeline, stream->timestamp_floor, stream->floor_exclusive);
    if (stream->has_buffered_flush_sequence &&
        stream->session.stream_type == AIRPLAY_STREAM_TYPE_BUFFERED)
        buffered_audio_flush(&server->pipeline.buffered, stream->buffered_flush_sequence);
    return 0;
}

static void update_audio_volume(airplay_server_t *server,
                                const rtsp_stream_state_t *stream,
                                audio_loop_state_t *state)
{
    if (state->volume_db == stream->volume_db)
        return;

    state->volume_db = stream->volume_db;
    audio_pipeline_set_volume(&server->pipeline, state->volume_db);
}

/* Thread 3: audio data path. Reads the RTSP state and owns the audio pipeline
 * sockets: RTP on AIRPLAY_AUDIO_PORT, RTCP/control on AIRPLAY_CONTROL_PORT,
 * and NTP timing on AIRPLAY_TIMING_PORT. It decodes audio and writes PCM to
 * the platform output; it does not accept RTSP control requests. */
void airplay_audio_main(void *arg)
{
    airplay_server_t *server = arg;
    audio_loop_state_t state = {0};
    while (!airplay_server_is_stopping(server))
    {
        rtsp_stream_state_t stream;
        rtsp_get_stream_state(&server->rtsp, &stream);
        update_audio_transport(server, &stream, &state); // Update the RTP sender endpoint for buffered audio and RAOP timing exchanges.
        if (!stream.recording || !stream.has_session) // Stop audio when the sender has stopped streaming or the session is gone.
        {
            // If the audio is running, stop it.
            if (server->audio) 
                close_audio(server); 
        }
        else
        {
            if (start_audio_session(server, &stream, &state) != 0)
            {
                server_fail(server);
                break;
            }
            update_audio_volume(server, &stream, &state);
        }
       
        if (stream.session.stream_type != AIRPLAY_STREAM_TYPE_REALTIME) // For buffered audio and RAOP timing, update the PTP anchor if it has changed.
        {
            server->pipeline.anchor = stream.anchor;
            ptp_sync_set_clock(&server->pipeline.ptp, stream.anchor.clock_id);
        }

        if (audio_pipeline_poll(&server->pipeline, 20) != 0) // Poll the audio pipeline for RTP packets, decode audio, and send to output callback.
            server_fail(server);
    }
    close_audio(server);
}

void airplay_server_deinit(airplay_server_t *server)
{
    if (!server || !server->initialized)
        return;
    if (server->audio)
    {
        audio_close(server->audio);
        server->audio = NULL;
    }
    if (server->discovery_initialized)
        airplay_discovery_deinit(&server->discovery);
    if (server->audio_initialized)
        audio_pipeline_close(&server->pipeline);
    if (server->rtsp_initialized)
        rtsp_server_close(&server->rtsp);
    os_mutex_deinit(&server->control_lock);
    crypto_memory_deinit();
    server->initialized = server->discovery_initialized = 0;
    server->audio_initialized = server->rtsp_initialized = 0;
}
