#include "airplay_auth.h"
#include "crypto/crypto_memory.h"

#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

static const char airport_private_key[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
    "wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
    "wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
    "/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
    "UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
    "BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
    "LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
    "NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
    "lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
    "aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
    "a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
    "oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
    "oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
    "k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
    "AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
    "cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
    "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
    "17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
    "1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
    "LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
    "2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
    "-----END RSA PRIVATE KEY-----\0";

/**
 * @brief base64_decode_padded.
 * @param str Parameter named str.
 * @param out Parameter named out.
 * @param out_size Parameter named out_size.
 * @param padded Parameter named padded.
 * @param padded_size Parameter named padded_size.
 * @param out_len Parameter named out_len.
 * @return Function result.
 */
static int base64_decode_padded(const char *str,
                                uint8_t *out,
                                size_t out_size,
                                char *padded,
                                size_t padded_size,
                                size_t *out_len)
{
    if (!str || !out || !padded)
        return -1;

    size_t input_len = strlen(str);
    size_t padded_len = input_len;
    size_t mod = input_len % 4;
    if (mod != 0)
        padded_len += (4 - mod);

    if (padded_size < padded_len + 1)
        return -1;

    memcpy(padded, str, input_len);
    for (size_t i = input_len; i < padded_len; i++)
        padded[i] = '=';
    padded[padded_len] = '\0';

    size_t decoded_len = 0;
    if (mbedtls_base64_decode(NULL, 0, &decoded_len, (const unsigned char *)padded, padded_len) !=
        MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return -1;

    if (decoded_len > out_size)
        return -1;

    if (mbedtls_base64_decode(
            out, out_size, &decoded_len, (const unsigned char *)padded, padded_len) != 0)
        return -1;

    if (out_len)
        *out_len = decoded_len;
    return 0;
}

int apple_challenge_response(const char *challenge,
                             const uint8_t *ip_addr,
                             const uint8_t *mac_addr,
                             char *response_out,
                             size_t response_out_len,
                             airplay_auth_scratch_t *scratch)
{
    if (crypto_memory_init() != 0 || !challenge || !ip_addr || !mac_addr || !response_out ||
        response_out_len == 0 || !scratch)
        return -1;

    size_t challenge_len = 0;
    if (base64_decode_padded(challenge,
                             scratch->decoded,
                             sizeof(scratch->decoded),
                             scratch->padded,
                             sizeof(scratch->padded),
                             &challenge_len) != 0)
        return -1;

    uint8_t message[48];
    memset(message, 0, sizeof(message));

    size_t copy_len = challenge_len > 16 ? 16 : challenge_len;
    memcpy(message, scratch->decoded, copy_len);
    memcpy(message + copy_len, ip_addr, 4);
    memcpy(message + copy_len + 4, mac_addr, 6);

    size_t message_len = copy_len + 10;
    if (message_len < 32)
        message_len = 32;

    mbedtls_pk_context pk_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "apple_challenge";
    int ret;

    mbedtls_pk_init(&pk_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(
        &ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    if (ret != 0)
        goto cleanup;

    ret = mbedtls_pk_parse_key(
        &pk_ctx, (const unsigned char *)airport_private_key, sizeof(airport_private_key), NULL, 0);
    if (ret != 0)
        goto cleanup;

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk_ctx);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);

    uint8_t signature[256];
    ret = mbedtls_rsa_pkcs1_encrypt(rsa,
                                    mbedtls_ctr_drbg_random,
                                    &ctr_drbg,
                                    MBEDTLS_RSA_PRIVATE,
                                    message_len,
                                    message,
                                    signature);
    if (ret != 0)
        goto cleanup;

    size_t encoded_len = 0;
    if (mbedtls_base64_encode(NULL, 0, &encoded_len, signature, rsa->len) !=
        MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        ret = -1;
        goto cleanup;
    }

    if (encoded_len + 1 > response_out_len)
    {
        ret = -1;
        goto cleanup;
    }

    if (mbedtls_base64_encode(
            (unsigned char *)response_out, response_out_len, &encoded_len, signature, rsa->len) !=
        0)
    {
        ret = -1;
        goto cleanup;
    }

    response_out[encoded_len] = '\0';
    char *padding = strchr(response_out, '=');
    if (padding)
        *padding = '\0';

cleanup:
    mbedtls_pk_free(&pk_ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}
