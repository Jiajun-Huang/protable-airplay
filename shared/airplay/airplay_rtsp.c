#include "airplay_rtsp.h"
#include "airplay_auth.h"
#include "network_util.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sdp_session_t g_announced_session;
static volatile int g_has_announced_session = 0;
static volatile int g_airplay_rtsp_recording = 0;
static volatile float g_airplay_volume_db = -20.0f;
static volatile unsigned int g_airplay_volume_version = 0;

static int request_body_contains_key(const rtsp_request_t *request, const char *key)
{
    char body[256];
    size_t n;

    if (!request || !request->body || request->body_len == 0 || !key)
        return 0;

    n = request->body_len;
    if (n >= sizeof(body))
        n = sizeof(body) - 1;

    memcpy(body, request->body, n);
    body[n] = '\0';

    return strstr(body, key) != NULL;
}

static int parse_body_volume_db(const rtsp_request_t *request, float *out_db)
{
    char body[256];
    char *p;
    char *endptr;
    float v;
    size_t n;

    if (!request || !request->body || request->body_len == 0 || !out_db)
        return -1;

    n = request->body_len;
    if (n >= sizeof(body))
        n = sizeof(body) - 1;

    memcpy(body, request->body, n);
    body[n] = '\0';

    p = strstr(body, "volume:");
    if (!p)
        return -1;

    p += 7;
    while (*p == ' ' || *p == '\t')
        p++;

    v = strtof(p, &endptr);
    if (endptr == p)
        return -1;

    if (v > 0.0f)
        v = 0.0f;
    if (v < -144.0f)
        v = -144.0f;

    *out_db = v;
    return 0;
}

static int parse_transport_port(const char *transport, const char *key, uint16_t *out_port)
{
    const char *p;
    char *endptr;
    unsigned long v;

    if (!transport || !key || !out_port)
        return -1;

    p = strstr(transport, key);
    if (!p)
        return -1;
    p += strlen(key);
    if (*p != '=')
        return -1;
    p++;

    v = strtoul(p, &endptr, 10);
    if (endptr == p || v > 65535)
        return -1;

    *out_port = (uint16_t)v;
    return 0;
}

static const char *get_header_value(const rtsp_request_t *request, const char *name)
{
    size_t i;

    if (!request || !name)
        return NULL;

    for (i = 0; i < request->header_count; i++)
    {
        if (_stricmp(request->headers[i].name, name) == 0)
            return request->headers[i].value;
    }

    return NULL;
}

static int parse_mac_hex(const char *hex, uint8_t mac[6])
{
    unsigned int v[6];
    if (!hex || !mac)
        return -1;

    if (sscanf(hex, "%2x%2x%2x%2x%2x%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return -1;

    for (int i = 0; i < 6; i++)
        mac[i] = (uint8_t)v[i];

    return 0;
}

int airplay_rtsp_options(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    char extra_headers[2048];
    const char *challenge;
    uint8_t ip_bytes[4];
    uint8_t mac_addr[6];

    (void)instance;

    snprintf(extra_headers, sizeof(extra_headers),
             "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET\r\n");

    LOG_RTSP_INFO("Sending OPTIONS response to %s:%u\n", client->ip, client->port);

    challenge = get_header_value(request, "Apple-Challenge");
    if (challenge && challenge[0] != '\0')
    {
        if (sscanf("10.0.0.178", "%hhu.%hhu.%hhu.%hhu",
                   &ip_bytes[0], &ip_bytes[1], &ip_bytes[2], &ip_bytes[3]) == 4 &&
            parse_mac_hex("1CCE516D2E30", mac_addr) == 0)
        {
            char apple_response[384];

            if (apple_challenge_response(challenge, ip_bytes, mac_addr, apple_response) == 0)
            {
                strncat(extra_headers, "Apple-Response: ", sizeof(extra_headers) - strlen(extra_headers) - 1);
                strncat(extra_headers, apple_response, sizeof(extra_headers) - strlen(extra_headers) - 1);
                strncat(extra_headers, "\r\n", sizeof(extra_headers) - strlen(extra_headers) - 1);
                LOG_RTSP_INFO("Added Apple-Response header\n");
            }
            else
            {
                LOG_RTSP_ERROR("Failed to generate Apple-Response\n");
            }
        }
    }

    return rtsp_send_response(client, 200, "OK", request->cseq, extra_headers, NULL, 0);
}
int airplay_rtsp_describe(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}
int airplay_rtsp_announce(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;

    if (request && request->body && request->body_len > 0)
    {
        sdp_session_t parsed;
        if (sdp_parse(request->body, request->body_len, &parsed) == 0)
        {
            g_announced_session = parsed;
            g_has_announced_session = 1;
            LOG_RTSP_INFO("ANNOUNCE parsed: codec=%d rate=%u channels=%u bits=%u\n",
                          parsed.codec, parsed.sample_rate, parsed.channels, parsed.bits_per_sample);
        }
        else
        {
            LOG_RTSP_ERROR("ANNOUNCE SDP parse failed\n");
        }
    }

    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_post(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const char *content_type_hdr;
    (void)instance;

    content_type_hdr = get_header_value(request, "Content-Type");
    LOG_RTSP_INFO("POST accepted (Content-Type=%s)\n",
                  content_type_hdr ? content_type_hdr : "none");
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_setup(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const char *transport_hdr;
    uint16_t client_timing = 0;
    uint16_t client_control = 0;
    char extra_headers[512];
    (void)instance;

    transport_hdr = get_header_value(request, "Transport");
    if (transport_hdr)
    {
        parse_transport_port(transport_hdr, "timing_port", &client_timing);
        parse_transport_port(transport_hdr, "control_port", &client_control);
    }

    LOG_RTSP_INFO("SETUP cseq=%u client=%s:%u timing=%u control=%u\n",
                  request->cseq,
                  client->ip,
                  client->port,
                  client_timing,
                  client_control);

    snprintf(extra_headers, sizeof(extra_headers),
             "Session: 00000001\r\n"
             "Transport: RTP/AVP/UDP;unicast;mode=record;server_port=6000;control_port=6001;timing_port=6002\r\n"
             "Audio-Jack-Status: connected\r\n");
    return rtsp_send_response(client, 200, "OK", request->cseq, extra_headers, NULL, 0);
}

int airplay_rtsp_get_parameter(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const uint8_t *body = NULL;
    size_t body_len = 0;
    const char *ct = "Content-Type: text/parameters\r\n";
    char volume_body[64];
    (void)instance;

    if (request_body_contains_key(request, "volume"))
    {
        snprintf(volume_body, sizeof(volume_body), "volume: %.6f\r\n", g_airplay_volume_db);
        body = (const uint8_t *)volume_body;
        body_len = strlen(volume_body);
    }

    return rtsp_send_response(client, 200, "OK", request->cseq, ct, body, body_len);
}

int airplay_rtsp_set_parameter(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;

    if (request && request->body && request->body_len > 0)
    {
        const char *content_type = get_header_value(request, "Content-Type");
        float volume_db;

        // Only treat text/parameters payload as volume/control data.
        // Metadata/artwork SET_PARAMETER bodies must not alter runtime volume.
        if (content_type && strstr(content_type, "text/parameters") &&
            parse_body_volume_db(request, &volume_db) == 0)
        {
            g_airplay_volume_db = volume_db;
            g_airplay_volume_version++;
            LOG_RTSP_INFO("SET_PARAMETER volume=%.3f dB\n", volume_db);
        }
    }

    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_flush(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_flushbuffered(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_teardown(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;
    g_airplay_rtsp_recording = 0;
    airplay_rtsp_clear_announced_session();
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_pause(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    (void)instance;
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_record(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const char *record_headers = "Session: 00000001\r\nAudio-Latency: 2205\r\n";
    const char *session_hdr = get_header_value(request, "Session");
    (void)instance;

    g_airplay_rtsp_recording = 1;
    LOG_RTSP_INFO("RECORD cseq=%u client=%s:%u session=%s state=recording\n",
                  request->cseq,
                  client->ip,
                  client->port,
                  (session_hdr && session_hdr[0] != '\0') ? session_hdr : "none");
    return rtsp_send_response(client, 200, "OK", request->cseq, record_headers, NULL, 0);
}

int airplay_rtsp_play(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const char *session_hdr = get_header_value(request, "Session");
    char extra_headers[192];
    (void)instance;

    if (session_hdr && session_hdr[0] != '\0')
    {
        snprintf(extra_headers, sizeof(extra_headers), "Session: %s\r\n", session_hdr);
        return rtsp_send_response(client, 200, "OK", request->cseq, extra_headers, NULL, 0);
    }

    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}

int airplay_rtsp_get_announced_session(sdp_session_t *out_session)
{
    if (!out_session || !g_has_announced_session)
        return -1;

    *out_session = g_announced_session;
    return 0;
}

void airplay_rtsp_clear_announced_session(void)
{
    memset(&g_announced_session, 0, sizeof(g_announced_session));
    g_has_announced_session = 0;
}

int airplay_rtsp_is_recording(void)
{
    return g_airplay_rtsp_recording;
}

float airplay_rtsp_get_volume_db(void)
{
    return g_airplay_volume_db;
}

unsigned int airplay_rtsp_get_volume_version(void)
{
    return g_airplay_volume_version;
}