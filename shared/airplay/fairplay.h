#ifndef AIRPLAY_FAIRPLAY_H
#define AIRPLAY_FAIRPLAY_H

#include <stddef.h>
#include <stdint.h>

#define FAIRPLAY_RESPONSE_MAX 142

/* FairPlay setup responder used before AirPlay 2 stream key negotiation. */

/* Version 3 setup handshake only. Zero-initialize stage for each connection.
 * Returns the
 * response length, or -1 without changing stage on invalid input. */
int fairplay_setup(
    uint8_t *stage, const uint8_t *request, size_t size, uint8_t *response, size_t capacity);

#endif
