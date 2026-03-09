#ifndef AIRPLAY_AUTH_H
#define AIRPLAY_AUTH_H

#include <stdint.h>

/**
 * Generate Apple-Challenge response for AirPlay RTSP OPTIONS.
 * @param challenge Base64-encoded challenge from client
 * @param ip_addr 4-byte IPv4 address (network byte order)
 * @param mac_addr 6-byte MAC address
 * @param response_out Output buffer for base64-encoded response (caller must provide 384+ bytes)
 * @return 0 on success, -1 on error
 */
int apple_challenge_response(const char *challenge,
                             const uint8_t *ip_addr,
                             const uint8_t *mac_addr,
                             char *response_out);

#endif // AIRPLAY_AUTH_H
