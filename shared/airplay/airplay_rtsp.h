#ifndef AIRPLAY_RTSP_H
#define AIRPLAY_RTSP_H

#include "protocol/rtsp.h"

/* AirPlay 1 / RAOP RTSP method handlers. They translate textual RTSP and SDP
 * requests into the protocol-neutral stream state consumed by the audio service. */

/* Report supported methods and answer Apple-Challenge when present. */
int airplay_rtsp_options(rtsp_instance_t *instance,
                         rtsp_client_t *client,
                         const rtsp_request_t *request);
/* Answer a DESCRIBE probe accepted by the RAOP control path. */
int airplay_rtsp_describe(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Parse ANNOUNCE SDP and publish codec, encryption, and RTP session state. */
int airplay_rtsp_announce(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Accept compatible RAOP POST requests that require no response body. */
int airplay_rtsp_post(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request);
/* Negotiate the RAOP UDP audio, control, and timing ports. */
int airplay_rtsp_setup(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Return requested stream parameters such as volume. */
int airplay_rtsp_get_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* Apply stream parameters such as volume and metadata. */
int airplay_rtsp_set_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* Flush realtime audio through the RTP-Info timestamp boundary. */
int airplay_rtsp_flush(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Handle the fallback form of FLUSHBUFFERED for a non-AirPlay-2 request. */
int airplay_rtsp_flushbuffered(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* End the current stream and release its ownership. */
int airplay_rtsp_teardown(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Pause output while retaining the negotiated session. */
int airplay_rtsp_pause(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Start or resume recording from the negotiated sender. */
int airplay_rtsp_record(rtsp_instance_t *instance,
                        rtsp_client_t *client,
                        const rtsp_request_t *request);
/* Start playback for senders that use PLAY instead of RECORD. */
int airplay_rtsp_play(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request);

#endif
