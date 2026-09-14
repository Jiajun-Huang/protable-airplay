#include "crypto/crypto.h"
#include "crypto/crypto_memory.h"
#include "util/log.h"

#include <stdio.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
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

int crypto_rsa_decrypt_aes_key(const uint8_t *encrypted_key,
                               size_t encrypted_len,
                               uint8_t *decrypted_key)
{
    if (crypto_memory_init() != 0 || !encrypted_key || !decrypted_key || encrypted_len == 0)
        return -1;

    int rc = -1;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "airplay_rsa_decrypt";
    size_t out_len = 0;
    unsigned char outbuf[512];

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    if (mbedtls_ctr_drbg_seed(
            &ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers)) !=
        0)
        goto cleanup;

    if (mbedtls_pk_parse_key(&pk,
                             (const unsigned char *)airport_private_key,
                             sizeof(airport_private_key),
                             NULL,
                             0) != 0)
        goto cleanup;

    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA))
        goto cleanup;

    // AirPlay rsaaeskey uses RSA-OAEP with SHA1.
    // Changing padding/hash here will break key unwrap for valid senders.
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);

    if (mbedtls_pk_decrypt(&pk,
                           encrypted_key,
                           encrypted_len,
                           outbuf,
                           &out_len,
                           sizeof(outbuf),
                           mbedtls_ctr_drbg_random,
                           &ctr_drbg) != 0)
        goto cleanup;

    if (out_len < 16)
        goto cleanup;

    memcpy(decrypted_key, outbuf, 16);
    rc = 0;

cleanup:
    memset(outbuf, 0, sizeof(outbuf));
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return rc;
}

int crypto_aes_init(crypto_aes_context_t *ctx, const uint8_t *aes_key, const uint8_t *aes_iv)
{
    if (!ctx || !aes_key || !aes_iv)
        return -1;

    memcpy(ctx->key, aes_key, 16);
    memcpy(ctx->iv, aes_iv, 16);
    memcpy(ctx->state, aes_iv, 16);

    LOG_INFO("crypto", "AES-128-CBC initialized\n");
    return 0;
}

int crypto_aes_decrypt(crypto_aes_context_t *ctx, const uint8_t *input, uint8_t *output, size_t len)
{
    if (!ctx || !input || !output || (len % 16) != 0)
        return -1;

    mbedtls_aes_context aes;
    unsigned char iv[16];
    mbedtls_aes_init(&aes);
    // AirPlay audio decryption resets CBC IV for each packet in this receiver path.
    // Do not carry IV state across packets unless protocol behavior is re-validated end-to-end.
    memcpy(iv, ctx->iv, sizeof(iv));

    int ret = mbedtls_aes_setkey_dec(&aes, ctx->key, 128);
    if (ret == 0)
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, input, output);

    mbedtls_aes_free(&aes);
    return ret == 0 ? 0 : -1;
}

int crypto_airplay2_decrypt_rtp(const uint8_t key[32],
                                const uint8_t *full_packet,
                                size_t full_packet_len,
                                size_t payload_offset,
                                size_t payload_len,
                                uint8_t *output,
                                size_t output_capacity,
                                size_t *output_len)
{
    mbedtls_chachapoly_context context;
    uint8_t nonce[12] = {0};
    const uint8_t *payload;
    const uint8_t *tag;
    size_t encrypted_len;
    int ret;

    if (!key || !full_packet || full_packet_len < 12 || !output || !output_len ||
        payload_offset > full_packet_len || payload_len > full_packet_len - payload_offset ||
        payload_len < 24)
        return -1;
    encrypted_len = payload_len - 8;
    if (encrypted_len < 16 || encrypted_len - 16 > output_capacity)
        return -1;

    payload = full_packet + payload_offset;
    memcpy(nonce + 4, payload + encrypted_len, 8);
    tag = payload + encrypted_len - 16;

    mbedtls_chachapoly_init(&context);
    ret = mbedtls_chachapoly_setkey(&context, key);
    if (ret == 0)
        ret = mbedtls_chachapoly_auth_decrypt(
            &context, encrypted_len - 16, nonce, full_packet + 4, 8, tag, payload, output);
    mbedtls_chachapoly_free(&context);
    if (ret != 0)
        return -1;
    *output_len = encrypted_len - 16;
    return 0;
}
