#include "rtsp.h"
#include "sdp.h"

int airplay_rtsp_options(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_describe(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_announce(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_post(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_setup(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_get_parameter(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_set_parameter(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_flush(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_flushbuffered(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_teardown(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_pause(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_record(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_play(rtsp_instance_t *instance, tcp_client_t *client, const rtsp_request_t *request);
int airplay_rtsp_get_announced_session(sdp_session_t *out_session);
void airplay_rtsp_clear_announced_session(void);
int airplay_rtsp_is_recording(void);
