#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stdint.h>
#include <stddef.h>

/**
 * Base64 encode data
 * @param data Input data
 * @param len Length of input data
 * @param out_len Output parameter for encoded length (can be NULL)
 * @return Newly allocated base64 string (caller must free), or NULL on error
 */
char *base64_encode(const uint8_t *data, size_t len, size_t *out_len);

/**
 * Base64 decode data
 * @param str Input base64 string (can have missing padding)
 * @param out_len Output parameter for decoded length
 * @return Newly allocated decoded data (caller must free), or NULL on error
 */
uint8_t *base64_decode(const char *str, size_t *out_len);

/**
 * Generate Apple-Challenge response
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

#endif // CRYPTO_UTILS_H
