#ifndef AIRPLAY_AUTH_H
#define AIRPLAY_AUTH_H

#include <stdint.h>
#include <stddef.h>

/**
 * Generate Apple-Challenge response for AirPlay RTSP OPTIONS.
 * @param challenge Base64-encoded challenge from client
 * @param ip_addr 4-byte IPv4 address (network byte order)
 * @param mac_addr 6-byte MAC address
 * @param response_out Output buffer for base64-encoded response
 * @param response_out_len Output buffer size (384+ recommended)
 * @param scratch Scratch buffer for base64 decode
 * @return 0 on success, -1 on error
 */

#ifndef AIRPLAY_AUTH_PADDED_MAX
#define AIRPLAY_AUTH_PADDED_MAX 256
#endif

#ifndef AIRPLAY_AUTH_DECODED_MAX
#define AIRPLAY_AUTH_DECODED_MAX 64
#endif

typedef struct
{
    char padded[AIRPLAY_AUTH_PADDED_MAX];
    uint8_t decoded[AIRPLAY_AUTH_DECODED_MAX];
} airplay_auth_scratch_t;

int apple_challenge_response(const char *challenge,
                             const uint8_t *ip_addr,
                             const uint8_t *mac_addr,
                             char *response_out,
                             size_t response_out_len,
                             airplay_auth_scratch_t *scratch);

#endif // AIRPLAY_AUTH_H
