#ifndef AIRPLAY2_H
#define AIRPLAY2_H
#include "protocol/rtsp.h"

/* AirPlay 2 request handlers called by the shared RTSP method router. They own
 * pairing, FairPlay, binary-plist setup, playback anchors, and buffered flushes. */

/* Return receiver capabilities for GET /info. */
int airplay2_info(rtsp_instance_t *server, rtsp_client_t *client, const rtsp_request_t *request);

/* Accept an encrypted AirPlay 2 control-session RECORD request. */
int airplay2_record(rtsp_instance_t *server, rtsp_client_t *client, const rtsp_request_t *request);

/* Process one POST /fp-setup FairPlay exchange. */
int airplay2_fairplay_setup(rtsp_instance_t *server,
                            rtsp_client_t *client,
                            const rtsp_request_t *request);

/* Process one POST /pair-setup exchange and enable encrypted control records. */
int airplay2_pair_setup(rtsp_instance_t *server,
                        rtsp_client_t *client,
                        const rtsp_request_t *request);

/* Process an encrypted binary-plist initial or stream SETUP request. */
int airplay2_setup(rtsp_instance_t *server, rtsp_client_t *client, const rtsp_request_t *request);

/* Apply an encrypted SETRATEANCHORTIME playback-rate and PTP anchor update. */
int airplay2_set_rate_anchor_time(rtsp_instance_t *server,
                                  rtsp_client_t *client,
                                  const rtsp_request_t *request);

/* Apply an encrypted binary-plist FLUSHBUFFERED sequence boundary. */
int airplay2_flush_buffered(rtsp_instance_t *server,
                            rtsp_client_t *client,
                            const rtsp_request_t *request);

/* Return stream information for an encrypted POST /feedback request. */
int airplay2_feedback(rtsp_instance_t *server,
                      rtsp_client_t *client,
                      const rtsp_request_t *request);

/* Acknowledge an authenticated AirPlay 2 request that has no response body. */
int airplay2_acknowledge(rtsp_instance_t *server,
                         rtsp_client_t *client,
                         const rtsp_request_t *request);
#endif
