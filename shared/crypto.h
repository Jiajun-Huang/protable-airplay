#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Crypto utilities for AirPlay audio decryption
 * Handles RSA-OAEP for AES key decryption and AES-128-CBC for stream decryption
 * User manages memory for crypto contexts.
 */

/**
 * @brief AES-128-CBC context
 * User allocates and manages memory for this structure.
 */
typedef struct
{
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t state[16]; // Current IV for CBC
} crypto_aes_context_t;

/**
 * @brief Decrypt RSA-encrypted AES key
 * Uses the AirPort Express RSA key required by this RAOP authentication scheme.
 *
 * @param encrypted_key RSA-encrypted AES key (base64 or binary)
 * @param encrypted_len length of encrypted data
 * @param decrypted_key output buffer for 16-byte AES key
 * @return 0 on success, negative on error
 */
int crypto_rsa_decrypt_aes_key(const uint8_t *encrypted_key, size_t encrypted_len,
                               uint8_t *decrypted_key);

/**
 * @brief Initialize AES-128-CBC decryption context
 * User provides allocated context; this function initializes it.
 *
 * @param ctx pointer to user-allocated crypto_aes_context_t
 * @param aes_key 16-byte AES key
 * @param aes_iv 16-byte initialization vector
 * @return 0 on success, negative on error
 */
int crypto_aes_init(crypto_aes_context_t *ctx, const uint8_t *aes_key, const uint8_t *aes_iv);

/**
 * @brief Decrypt AirPlay audio packet with AES-128-CBC
 * @param ctx AES context from crypto_aes_init
 * @param input encrypted audio data
 * @param output decrypted audio data buffer
 * @param len length of data (must be multiple of 16)
 * @return 0 on success, negative on error
 */
int crypto_aes_decrypt(crypto_aes_context_t *ctx, const uint8_t *input, uint8_t *output, size_t len);

#endif // CRYPTO_H
