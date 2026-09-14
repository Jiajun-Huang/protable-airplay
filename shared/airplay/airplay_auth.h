#ifndef AIRPLAY_AUTH_H
#define AIRPLAY_AUTH_H

#include <stddef.h>
#include <stdint.h>

/* AirPlay 1 challenge-response authentication used by RTSP OPTIONS. */

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

/* Sign a Base64 Apple-Challenge with the receiver identity.
 * ip_addr contains four network-order bytes and mac_addr contains six bytes.
 * Returns zero on success and -1 for invalid input or cryptographic failure. */
int apple_challenge_response(const char *challenge,
                             const uint8_t *ip_addr,
                             const uint8_t *mac_addr,
                             char *response_out,
                             size_t response_out_len,
                             airplay_auth_scratch_t *scratch);

#endif // AIRPLAY_AUTH_H
