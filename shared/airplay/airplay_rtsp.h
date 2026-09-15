











#ifndef AIRPLAY_RTSP_H
#define AIRPLAY_RTSP_H

#include "protocol/rtsp.h"

/* AirPlay 1 / RAOP RTSP method handlers. They translate textual RTSP and SDP
 * requests into the protocol-neutral stream state consumed by the audio service. */

/* Report supported methods and answer Apple-Challenge when present. */
/**
 * @brief airplay_rtsp_options.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_options(rtsp_instance_t *instance,
                         rtsp_client_t *client,
                         const rtsp_request_t *request);
/* Answer a DESCRIBE probe accepted by the RAOP control path. */
/**
 * @brief airplay_rtsp_describe.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_describe(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Parse ANNOUNCE SDP and publish codec, encryption, and RTP session state. */
/**
 * @brief airplay_rtsp_announce.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_announce(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Accept compatible RAOP POST requests that require no response body. */
/**
 * @brief airplay_rtsp_post.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_post(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request);
/* Negotiate the RAOP UDP audio, control, and timing ports. */
/**
 * @brief airplay_rtsp_setup.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_setup(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Return requested stream parameters such as volume. */
/**
 * @brief airplay_rtsp_get_parameter.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_get_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* Apply stream parameters such as volume and metadata. */
/**
 * @brief airplay_rtsp_set_parameter.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_set_parameter(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* Flush realtime audio through the RTP-Info timestamp boundary. */
/**
 * @brief airplay_rtsp_flush.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_flush(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Handle the fallback form of FLUSHBUFFERED for a non-AirPlay-2 request. */
/**
 * @brief airplay_rtsp_flushbuffered.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_flushbuffered(rtsp_instance_t *instance,
                               rtsp_client_t *client,
                               const rtsp_request_t *request);
/* End the current stream and release its ownership. */
/**
 * @brief airplay_rtsp_teardown.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_teardown(rtsp_instance_t *instance,
                          rtsp_client_t *client,
                          const rtsp_request_t *request);
/* Pause output while retaining the negotiated session. */
/**
 * @brief airplay_rtsp_pause.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_pause(rtsp_instance_t *instance,
                       rtsp_client_t *client,
                       const rtsp_request_t *request);
/* Start or resume recording from the negotiated sender. */
/**
 * @brief airplay_rtsp_record.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_record(rtsp_instance_t *instance,
                        rtsp_client_t *client,
                        const rtsp_request_t *request);
/* Start playback for senders that use PLAY instead of RECORD. */
/**
 * @brief airplay_rtsp_play.
 * @param instance Parameter named instance.
 * @param client Parameter named client.
 * @param request Parameter named request.
 * @return Function result.
 */
int airplay_rtsp_play(rtsp_instance_t *instance,
                      rtsp_client_t *client,
                      const rtsp_request_t *request);

#endif
