#include "airplay_rtsp.h"
#include <stdio.h>

int airplay_rtsp_options(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
    const char *public_methods = "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET\r\n";

    printf("[RTSP] Sending OPTIONS response to %s:%u\n", client->ip, client->port);

    return rtsp_send_response(client, 200, "OK", request->cseq, public_methods, NULL, 0);
}
int airplay_rtsp_describe(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
}
int airplay_rtsp_announce(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request)
{
}