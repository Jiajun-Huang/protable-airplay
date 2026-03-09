#include "airplay_rtsp.h"
#include "crypto_utils.h"
#include "network_util.h"
#include <stdio.h>
#include <string.h>

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

    printf("[RTSP] Sending OPTIONS response to %s:%u\n", client->ip, client->port);

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
                printf("[RTSP] Added Apple-Response header\n");
            }
            else
            {
                printf("[RTSP] Failed to generate Apple-Response\n");
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
    return rtsp_send_response(client, 200, "OK", request->cseq, NULL, NULL, 0);
}