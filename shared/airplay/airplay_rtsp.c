#include "airplay_rtsp.h"
#include "airplay_auth.h"
#include "util/log.h"
#include "util/network_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief request_body_contains_key.
 * @param request Parameter named request.
 * @param key Parameter named key.
 * @return Function result.
 */
static int request_body_contains_key(const rtsp_request_t *request, const char *key)
{
    char body[256];
    size_t length;
    if (!request->body || !request->body_len)
        return 0;
    length = request->body_len < sizeof(body) ? request->body_len : sizeof(body) - 1;
    memcpy(body, request->body, length);
    body[length] = '\0';
    return strstr(body, key) != NULL;
}

/**
 * @brief parse_body_volume_db.
 * @param request Parameter named request.
 * @param out_db Parameter named out_db.
 * @return Function result.
 */
static int parse_body_volume_db(const rtsp_request_t *request, float *out_db)
{
    char body[256], *value, *end;
    size_t length;
    float volume;
    if (!request->body || !request->body_len)
        return -1;
    length = request->body_len < sizeof(body) ? request->body_len : sizeof(body) - 1;
    memcpy(body, request->body, length);
    body[length] = '\0';
    value = strstr(body, "volume:");
    if (!value)
        return -1;
    value += 7;
    volume = strtof(value, &end);
    if (end == value || !isfinite(volume))
        return -1;
    if (volume > 0.0f)
        volume = 0.0f;
    if (volume < -144.0f)
        volume = -144.0f;
    *out_db = volume;
    return 0;
}

/**
 * @brief get_header_value.
 * @param request Parameter named request.
 * @param name Parameter named name.
 * @return Function result.
 */
static const char *get_header_value(const rtsp_request_t *request, const char *name)
{
    size_t i;
    for (i = 0; i < request->header_count; ++i)
        if (net_ascii_casecmp(request->headers[i].name, name) == 0)
            return request->headers[i].value;
    return NULL;
}

/**
 * @brief parse_mac_hex.
 * @param hex Parameter named hex.
 * @param mac Parameter named mac.
 * @return Function result.
 */
static int parse_mac_hex(const char *hex, uint8_t mac[6])
{
    unsigned values[6];
    size_t i;
    if (strlen(hex) != 12 || sscanf(hex,
                                    "%2x%2x%2x%2x%2x%2x",
                                    &values[0],
                                    &values[1],
                                    &values[2],
                                    &values[3],
                                    &values[4],
                                    &values[5]) != 6)
        return -1;
    for (i = 0; i < 6; ++i)
        mac[i] = (uint8_t)values[i];
    return 0;
}

/**
 * @brief parameter_u32.
 * @param text Parameter named text.
 * @param key Parameter named key.
 * @param out Parameter named out.
 * @return Function result.
 */
static int parameter_u32(const char *text, const char *key, uint32_t *out)
{
    if (!text)
        return 0;
    size_t key_length = strlen(key);
    const char *p = text;
    while ((p = strstr(p, key)) != NULL)
    {
        if (p != text && p[-1] != ';' && p[-1] != ',' && p[-1] != ' ')
        {
            ++p;
            continue;
        }
        p += key_length;
        if (*p != '=')
            continue;
        ++p;
        if (*p < '0' || *p > '9')
            return -1;
        uint32_t value = 0;
        while (*p >= '0' && *p <= '9')
        {
            unsigned digit = (unsigned)(*p++ - '0');
            if (value > (UINT32_MAX - digit) / 10)
                return -1;
            value = value * 10 + digit;
        }
        if (*p && *p != ';' && *p != ',' && *p != ' ')
            return -1;
        *out = value;
        return 1;
    }
    return 0;
}

int airplay_rtsp_options(rtsp_instance_t *instance,
                         rtsp_client_t *client,
                         const rtsp_request_t *request)
{
    char extra_headers[768];
    const char *challenge = get_header_value(request, "Apple-Challenge");
    snprintf(extra_headers,
             sizeof(extra_headers),
             "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, FLUSHBUFFERED, TEARDOWN, OPTIONS, "
             "GET_PARAMETER, SET_PARAMETER, POST, GET, SETPEERS, SETRATEANCHORTIME\r\n");
    if (challenge && *challenge)
    {
        char local_ip[16], local_mac_hex[13];
        uint32_t ip;
        uint8_t ip_bytes[4], mac[6];
        os_mutex_lock(&instance->state_lock);
        memcpy(local_ip, instance->local_ip, sizeof(local_ip));
        memcpy(local_mac_hex, instance->local_mac_hex, sizeof(local_mac_hex));
        os_mutex_unlock(&instance->state_lock);
        if (net_str_to_ipv4(local_ip, &ip) == 0 && parse_mac_hex(local_mac_hex, mac) == 0)
        {
            char response[384];
            airplay_auth_scratch_t scratch;
            memcpy(ip_bytes, &ip, sizeof(ip_bytes));
            if (apple_challenge_response(
                    challenge, ip_bytes, mac, response, sizeof(response), &scratch) == 0)
            {
                size_t length = strlen(extra_headers);
                snprintf(extra_headers + length,
                         sizeof(extra_headers) - length,
                         "Apple-Response: %s\r\n",
                         response);
            }
            else
                LOG_ERROR( "Failed to generate Apple-Response\n");
        }
    }
    return rtsp_send_response(client, 200, "OK", request->cseq, extra_headers, NULL, 0);
}

int airplay_rtsp_describe(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_announce(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request)
{
    sdp_session_t parsed;
    char scratch[SDP_SCRATCH_MAX];
    if (!request->body || !request->body_len ||
        sdp_parse(request->body, request->body_len, &parsed, scratch, sizeof(scratch)) != 0 ||
        parsed.codec == SDP_CODEC_UNKNOWN)
        return rtsp_send_response(client, 400, "Bad Request", request->cseq, NULL, NULL, 0);

    os_mutex_lock(&instance->state_lock);
    instance->stream.session = parsed;
    instance->stream.has_session = 1;
    instance->stream.recording = 0;
    memset(&instance->stream.timing_peer, 0, sizeof(instance->stream.timing_peer));
    instance->stream.has_timestamp_floor = 0;
    ++instance->stream.generation;
    instance->stream_owner = client;
    os_mutex_unlock(&instance->state_lock);
    LOG_INFO(
             "ANNOUNCE parsed: codec=%d rate=%u channels=%u bits=%u\n",
             parsed.codec,
             parsed.sample_rate,
             parsed.channels,
             parsed.bits_per_sample);
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_post(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_setup(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request)
{
    const char *transport = get_header_value(request, "Transport");
    uint32_t timing_port = 0;
    if (transport &&
        (!strstr(transport, "RTP/AVP/UDP") ||
         parameter_u32(transport, "timing_port", &timing_port) < 0 || timing_port > 65535))
        return rtsp_send_response(
            client, 461, "Unsupported Transport", request->cseq, NULL, NULL, 0);
    os_mutex_lock(&instance->state_lock);
    if (instance->stream_owner == client)
    {
        instance->stream.timing_peer = client->peer;
        instance->stream.timing_peer.port = (uint16_t)timing_port;
        ++instance->stream.generation;
    }
    os_mutex_unlock(&instance->state_lock);
    LOG_INFO( "SETUP timing peer=%s:%u\n", client->peer.ip, timing_port);
    const char *headers =
        "Session: 00000001\r\n"
        "Transport: RTP/AVP/UDP;unicast;mode=record;server_port=" AIRPLAY_STRINGIFY(AIRPLAY_AUDIO_PORT) ";control_port=" AIRPLAY_STRINGIFY(
            AIRPLAY_CONTROL_PORT) ";timing_port=" AIRPLAY_STRINGIFY(AIRPLAY_TIMING_PORT) "\r\n"
                                                                                         "Audio-"
                                                                                         "Jack-"
                                                                                         "Status: "
                                                                                         "connected"
                                                                                         "\r\n";
    return rtsp_send_response(client, 200, "OK", request->cseq, headers, NULL, 0);
}

int airplay_rtsp_get_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request)
{
    char volume_body[64];
    const uint8_t *body = NULL;
    size_t length = 0;
    if (request_body_contains_key(request, "volume"))
    {
        float volume;
        os_mutex_lock(&instance->state_lock);
        volume = instance->stream.volume_db;
        os_mutex_unlock(&instance->state_lock);
        snprintf(volume_body, sizeof(volume_body), "volume: %.6f\r\n", volume);
        body = (const uint8_t *)volume_body;
        length = strlen(volume_body);
    }
    return rtsp_send_response(
        client, 200, "OK", request->cseq, "Content-Type: text/parameters\r\n", body, length);
}

int airplay_rtsp_set_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request)
{
    const char *content_type = get_header_value(request, "Content-Type");
    float volume;
    /* Metadata and artwork bodies do not carry audio control parameters. */
    if (content_type && strstr(content_type, "text/parameters") &&
        parse_body_volume_db(request, &volume) == 0)
    {
        os_mutex_lock(&instance->state_lock);
        instance->stream.volume_db = volume;
        os_mutex_unlock(&instance->state_lock);
    }
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_flush(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request)
{
    uint32_t timestamp = 0;
    int has_timestamp = parameter_u32(get_header_value(request, "RTP-Info"), "rtptime", &timestamp);
    if (has_timestamp < 0)
        return rtsp_send_response(client, 400, "Bad Request", request->cseq, NULL, NULL, 0);
    os_mutex_lock(&instance->state_lock);
    if (instance->stream_owner == client)
    {
        ++instance->stream.flush_generation;
        instance->stream.has_timestamp_floor = has_timestamp;
        instance->stream.timestamp_floor = timestamp;
        instance->stream.floor_exclusive = 1;
    }
    os_mutex_unlock(&instance->state_lock);
    LOG_INFO( "FLUSH rtptime=%u present=%d\n", timestamp, has_timestamp);
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_flushbuffered(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request)
{
    return airplay_rtsp_flush(instance, client, request);
}

int airplay_rtsp_teardown(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request)
{
    os_mutex_lock(&instance->state_lock);
    if (instance->stream_owner == client)
    {
        instance->stream.recording = 0;
        instance->stream.has_session = 0;
        memset(&instance->stream.session, 0, sizeof(instance->stream.session));
        ++instance->stream.generation;
        instance->stream_owner = NULL;
    }
    os_mutex_unlock(&instance->state_lock);
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_pause(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request)
{
    os_mutex_lock(&instance->state_lock);
    if (instance->stream_owner == client)
        instance->stream.recording = 0;
    os_mutex_unlock(&instance->state_lock);
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_record(rtsp_instance_t *instance,
                        rtsp_client_t *client,
                        const rtsp_request_t *request)
{
    int has_session;
    uint32_t timestamp = 0;
    int has_timestamp = parameter_u32(get_header_value(request, "RTP-Info"), "rtptime", &timestamp);
    if (has_timestamp < 0)
        return rtsp_send_response(client, 400, "Bad Request", request->cseq, NULL, NULL, 0);
    os_mutex_lock(&instance->state_lock);
    has_session = instance->stream.has_session && instance->stream_owner == client;
    if (has_session)
    {
        instance->stream.recording = 1;
        if (has_timestamp)
        {
            instance->stream.has_timestamp_floor = 1;
            instance->stream.timestamp_floor = timestamp;
            instance->stream.floor_exclusive = 0;
        }
    }
    os_mutex_unlock(&instance->state_lock);
    if (!has_session)
        return rtsp_send_response(
            client, 455, "Method Not Valid in This State", request->cseq, NULL, NULL, 0);
    return rtsp_send_response(client,
                              200,
                              "OK",
                              request->cseq,
                              "Session: 00000001\r\nAudio-Latency: " AIRPLAY_STRINGIFY(
                                  AIRPLAY_AUDIO_LATENCY_FRAMES) "\r\n",
                              NULL,
                              0);
}

int airplay_rtsp_play(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request)
{
    const char *session = get_header_value(request, "Session");
    char headers[288];
    (void)instance;
    if (session && *session)
    {
        snprintf(headers, sizeof(headers), "Session: %s\r\n", session);
        return rtsp_send_response(client, 200, "OK", request->cseq, headers, NULL, 0);
    }
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}
