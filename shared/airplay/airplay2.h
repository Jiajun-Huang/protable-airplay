#ifndef AIRPLAY2_H
#define AIRPLAY2_H
#include "rtsp.h"
/* handled is zero when the request belongs to the RAOP handler. */
int airplay2_handle(rtsp_instance_t *server, rtsp_client_t *client,
                    const rtsp_request_t *request, int *handled);
#endif
