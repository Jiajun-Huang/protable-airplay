#include "rtsp.h"

int airplay_rtsp_options(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_describe(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_announce(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
