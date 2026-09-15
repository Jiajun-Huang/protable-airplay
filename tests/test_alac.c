#include "codec/alac_decoder.h"
#include "crypto/crypto.h"
#include "fixtures/alac.h"
#include "util/log.h"
#include <mbedtls/aes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR( "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
/**
 * @brief check_decode.
 * @param data Parameter named data.
 * @param length Parameter named length.
 * @param expected Parameter named expected.
 * @param channels Parameter named channels.
 */
static void check_decode(const uint8_t *data,
                         size_t length,
                         const int16_t *expected,
                         unsigned channels)
{
    alac_decoder_t decoder;
    uint32_t fmtp[] = {352, 0, 16, 40, 10, 14, channels, 255, 0, 0, 44100};
    CHECK(alac_decoder_init(&decoder, fmtp, 11, 352, 16, channels, 44100) == 0);
    int16_t output[704];
    memset(output, 0x55, sizeof(output));
    size_t count = 0;
    CHECK(alac_decoder_decode_frame(&decoder, data, length, output, &count, 704) == 0);
    CHECK(count == 352 * channels);
    size_t mismatch = 0;
    for (size_t i = 0; i < count; ++i)
        if (output[i] != expected[i])
            ++mismatch;
    LOG_INFO(
             "ALAC channels=%u bytes=%zu samples=%zu mismatches=%zu\n",
             channels,
             length,
             count,
             mismatch);
    CHECK(mismatch == 0);
    CHECK(alac_decoder_decode_frame(&decoder, data, 2, output, &count, 704) < 0 && count == 0);
    uint8_t invalid[8] = {0xe0};
    CHECK(alac_decoder_decode_frame(&decoder, invalid, sizeof(invalid), output, &count, 704) < 0 &&
          count == 0);
    if (channels == 2)
        CHECK(alac_decoder_decode_frame(
                  &decoder, mono_packet, sizeof(mono_packet), output, &count, 704) < 0 &&
              count == 0);
    for (size_t short_length = 1; short_length + 3 < length; ++short_length)
        CHECK(alac_decoder_decode_frame(&decoder, data, short_length, output, &count, 704) < 0 &&
              count == 0);
    alac_decoder_close(&decoder);
}
/**
 * @brief check_encrypted_packet.
 */
static void check_encrypted_packet(void)
{
    /* Each packet starts with the same session IV; an incomplete AES block is plaintext. */
    const uint8_t key[16] = {0x2b,
                             0x7e,
                             0x15,
                             0x16,
                             0x28,
                             0xae,
                             0xd2,
                             0xa6,
                             0xab,
                             0xf7,
                             0x15,
                             0x88,
                             0x09,
                             0xcf,
                             0x4f,
                             0x3c};
    const uint8_t iv[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t encrypted[sizeof(tone_packet)], decrypted[sizeof(tone_packet)], working_iv[16];
    const size_t encrypted_bytes = sizeof(tone_packet) / 16 * 16;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    CHECK(mbedtls_aes_setkey_enc(&aes, key, 128) == 0);
    memcpy(working_iv, iv, sizeof(iv));
    CHECK(mbedtls_aes_crypt_cbc(
              &aes, MBEDTLS_AES_ENCRYPT, encrypted_bytes, working_iv, tone_packet, encrypted) == 0);
    memcpy(encrypted + encrypted_bytes,
           tone_packet + encrypted_bytes,
           sizeof(tone_packet) - encrypted_bytes);
    crypto_aes_context_t context;
    CHECK(crypto_aes_init(&context, key, iv) == 0);
    for (int i = 0; i < 2; ++i)
    {
        CHECK(crypto_aes_decrypt(&context, encrypted, decrypted, encrypted_bytes) == 0);
        memcpy(decrypted + encrypted_bytes,
               encrypted + encrypted_bytes,
               sizeof(tone_packet) - encrypted_bytes);
        CHECK(memcmp(decrypted, tone_packet, sizeof(tone_packet)) == 0);
        check_decode(decrypted, sizeof(decrypted), tone_pcm, 2);
    }
    mbedtls_aes_free(&aes);
}
int main(void)
{
    check_decode(tone_packet, sizeof(tone_packet), tone_pcm, 2);
    check_decode(silence_packet, sizeof(silence_packet), silence_pcm, 2);
    check_decode(mono_packet, sizeof(mono_packet), mono_pcm, 1);
    /* A real RAOP silence packet, independently decoded as 704 zero samples by FFmpeg. */
    const uint8_t raop_silence[] = {0x20, 0x00, 0x00, 0x04, 0x00, 0x13, 0x08, 0x09,
                                    0x81, 0xf8, 0xc1, 0xff, 0x80, 0x00, 0x00, 0x13,
                                    0x08, 0x09, 0x81, 0xf8, 0xc1, 0xff, 0x80, 0x00,
                                    0x00, 0xff, 0x80, 0xaf, 0xbf, 0xe0, 0x2b, 0xfc};
    check_decode(raop_silence, sizeof(raop_silence), silence_pcm, 2);
    check_encrypted_packet();
    return 0;
}
