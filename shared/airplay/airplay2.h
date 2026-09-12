#ifndef AIRPLAY2_H
#define AIRPLAY2_H
#include "protocol/rtsp.h"

/* AirPlay 2 control handler for discovery information, pairing, FairPlay,
 * binary-plist stream setup, playback anchors, and buffered flushes. */

/* Handle one AirPlay 2 request. handled is zero when the request belongs to
 * the AirPlay 1 fallback; otherwise the return value is the request result. */
int airplay2_handle(rtsp_instance_t *server,
                    rtsp_client_t *client,
                    const rtsp_request_t *request,
                    int *handled);
#endif
