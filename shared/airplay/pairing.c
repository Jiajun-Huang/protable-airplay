#include "pairing.h"
#include <mbedtls/bignum.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/dhm.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha512.h>
#include <stdlib.h>
#include <string.h>

#define SRP_BYTES 384
#define TRY(call)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        if ((call) != 0)                                                                           \
            goto done;                                                                             \
    } while (0)
static const uint8_t prime[SRP_BYTES] = MBEDTLS_DHM_RFC3526_MODP_3072_P_BIN;
typedef struct
{
    mbedtls_mpi n, v, b;
    uint8_t salt[16], public_key[SRP_BYTES];
} srp_t;

static void free_srp(pairing_t *p)
{
    srp_t *s = p->srp;
    if (!s)
        return;
    mbedtls_mpi_free(&s->n);
    mbedtls_mpi_free(&s->v);
    mbedtls_mpi_free(&s->b);
    mbedtls_platform_zeroize(s, sizeof(*s));
    free(s);
    p->srp = NULL;
}
void pairing_close(pairing_t *p)
{
    if (!p)
        return;
    free_srp(p);
    mbedtls_platform_zeroize(p, sizeof(*p));
}

/* Concatenate TLV8 fragments, rejecting truncation and duplicate scalar fields. */
static int tlv_get(
    const uint8_t *in, size_t size, uint8_t type, uint8_t *out, size_t capacity, size_t *length)
{
    size_t pos = 0, used = 0;
    int found = 0, previous = -1;
    unsigned previous_size = 0;
    while (pos < size)
    {
        if (size - pos < 2)
            return -1;
        unsigned t = in[pos++], n = in[pos++];
        if (n > size - pos)
            return -1;
        if (t == type)
        {
            if ((found && (previous != type || previous_size != 255)) || n > capacity - used)
                return -1;
            if (n)
                memcpy(out + used, in + pos, n);
            used += n;
            found = 1;
        }
        previous = (int)t;
        previous_size = n;
        pos += n;
    }
    *length = used;
    return found ? 0 : -1;
}
static int tlv_put(
    uint8_t *out, size_t capacity, size_t *used, uint8_t type, const uint8_t *data, size_t size)
{
    do
    {
        size_t n = size > 255 ? 255 : size;
        if (*used > capacity || n + 2 > capacity - *used)
            return -1;
        out[(*used)++] = type;
        out[(*used)++] = (uint8_t)n;
        if (n)
            memcpy(out + *used, data, n);
        *used += n;
        data += n;
        size -= n;
    } while (size);
    return 0;
}
static int hash_parts(uint8_t out[64],
                      const uint8_t *const *parts,
                      const size_t *sizes,
                      size_t count)
{
    mbedtls_sha512_context h;
    mbedtls_sha512_init(&h);
    int result = mbedtls_sha512_starts_ret(&h, 0);
    for (size_t i = 0; !result && i < count; ++i)
        result = mbedtls_sha512_update_ret(&h, parts[i], sizes[i]);
    if (!result)
        result = mbedtls_sha512_finish_ret(&h, out);
    mbedtls_sha512_free(&h);
    return result;
}

static int begin(pairing_t *p)
{
    int result = -1;
    mbedtls_mpi g, x, k, temp, pub;
    mbedtls_mpi_init(&g);
    mbedtls_mpi_init(&x);
    mbedtls_mpi_init(&k);
    mbedtls_mpi_init(&temp);
    mbedtls_mpi_init(&pub);
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    uint8_t hash[64], padded_g[SRP_BYTES] = {0}, random[32];
    pairing_close(p);
    srp_t *s = calloc(1, sizeof(*s));
    if (!s)
        goto done;
    p->srp = s;
    mbedtls_mpi_init(&s->n);
    mbedtls_mpi_init(&s->v);
    mbedtls_mpi_init(&s->b);
    TRY(mbedtls_ctr_drbg_seed(
        &rng, mbedtls_entropy_func, &entropy, (const uint8_t *)"AirPlay-Pair-Setup", 18));
    TRY(mbedtls_ctr_drbg_random(&rng, s->salt, sizeof(s->salt)));
    TRY(mbedtls_ctr_drbg_random(&rng, random, sizeof(random)));
    TRY(mbedtls_mpi_read_binary(&s->n, prime, sizeof(prime)));
    TRY(mbedtls_mpi_lset(&g, 5));
    TRY(mbedtls_mpi_read_binary(&s->b, random, sizeof(random)));
    /* The transient pairing credential is fixed by the protocol. */
    TRY(mbedtls_sha512_ret((const uint8_t *)"Pair-Setup:3939", 15, hash, 0));
    const uint8_t *xparts[] = {s->salt, hash};
    const size_t xsizes[] = {sizeof(s->salt), sizeof(hash)};
    TRY(hash_parts(hash, xparts, xsizes, 2));
    TRY(mbedtls_mpi_read_binary(&x, hash, sizeof(hash)));
    TRY(mbedtls_mpi_exp_mod(&s->v, &g, &x, &s->n, NULL));
    padded_g[SRP_BYTES - 1] = 5;
    const uint8_t *kparts[] = {prime, padded_g};
    const size_t ksizes[] = {sizeof(prime), sizeof(padded_g)};
    TRY(hash_parts(hash, kparts, ksizes, 2));
    TRY(mbedtls_mpi_read_binary(&k, hash, sizeof(hash)));
    TRY(mbedtls_mpi_exp_mod(&pub, &g, &s->b, &s->n, NULL));
    TRY(mbedtls_mpi_mul_mpi(&temp, &k, &s->v));
    TRY(mbedtls_mpi_add_mpi(&pub, &pub, &temp));
    TRY(mbedtls_mpi_mod_mpi(&pub, &pub, &s->n));
    TRY(mbedtls_mpi_write_binary(&pub, s->public_key, sizeof(s->public_key)));
    result = 0;
done:
    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(random, sizeof(random));
    mbedtls_mpi_free(&g);
    mbedtls_mpi_free(&x);
    mbedtls_mpi_free(&k);
    mbedtls_mpi_free(&temp);
    mbedtls_mpi_free(&pub);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    if (result)
        free_srp(p);
    return result;
}

static void minimal(const uint8_t **data, size_t *size)
{
    while (*size > 1 && **data == 0)
    {
        ++*data;
        --*size;
    }
}
static int verify(
    pairing_t *p, const uint8_t *a, size_t a_size, const uint8_t proof[64], uint8_t answer[64])
{
    srp_t *s = p->srp;
    if (!s || !a_size || a_size > SRP_BYTES)
        return -1;
    int result = -1;
    mbedtls_mpi A, u, temp, secret;
    mbedtls_mpi_init(&A);
    mbedtls_mpi_init(&u);
    mbedtls_mpi_init(&temp);
    mbedtls_mpi_init(&secret);
    uint8_t padded_a[SRP_BYTES] = {0}, hash[64], key[64], secret_bytes[SRP_BYTES];
    uint8_t hn[64], hg[64], hi[64], expected[64], generator = 5;
    memcpy(padded_a + SRP_BYTES - a_size, a, a_size);
    TRY(mbedtls_mpi_read_binary(&A, a, a_size));
    TRY(mbedtls_mpi_mod_mpi(&temp, &A, &s->n));
    if (mbedtls_mpi_cmp_int(&temp, 0) == 0)
        goto done;
    const uint8_t *uparts[] = {padded_a, s->public_key};
    const size_t usizes[] = {SRP_BYTES, SRP_BYTES};
    TRY(hash_parts(hash, uparts, usizes, 2));
    TRY(mbedtls_mpi_read_binary(&u, hash, sizeof(hash)));
    if (mbedtls_mpi_cmp_int(&u, 0) == 0)
        goto done;
    TRY(mbedtls_mpi_exp_mod(&temp, &s->v, &u, &s->n, NULL));
    TRY(mbedtls_mpi_mul_mpi(&temp, &temp, &A));
    TRY(mbedtls_mpi_mod_mpi(&temp, &temp, &s->n));
    TRY(mbedtls_mpi_exp_mod(&secret, &temp, &s->b, &s->n, NULL));
    size_t secret_size = mbedtls_mpi_size(&secret);
    if (!secret_size || secret_size > sizeof(secret_bytes))
        goto done;
    TRY(mbedtls_mpi_write_binary(&secret, secret_bytes, secret_size));
    TRY(mbedtls_sha512_ret(secret_bytes, secret_size, key, 0));
    TRY(mbedtls_sha512_ret(prime, sizeof(prime), hn, 0));
    TRY(mbedtls_sha512_ret(&generator, 1, hg, 0));
    TRY(mbedtls_sha512_ret((const uint8_t *)"Pair-Setup", 10, hi, 0));
    for (size_t i = 0; i < 64; ++i)
        hn[i] ^= hg[i];
    const uint8_t *parts[] = {hn, hi, s->salt, padded_a, s->public_key, key};
    size_t sizes[] = {64, 64, 16, SRP_BYTES, SRP_BYTES, 64};
    for (size_t i = 2; i <= 4; ++i)
        minimal(&parts[i], &sizes[i]);
    TRY(hash_parts(expected, parts, sizes, 6));
    unsigned difference = 0;
    for (size_t i = 0; i < 64; ++i)
        difference |= expected[i] ^ proof[i];
    if (difference)
        goto done;
    const uint8_t *mparts[] = {parts[3], proof, key};
    const size_t msizes[] = {sizes[3], 64, 64};
    TRY(hash_parts(answer, mparts, msizes, 3));
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    TRY(mbedtls_hkdf(md,
                     (const uint8_t *)"Control-Salt",
                     12,
                     key,
                     64,
                     (const uint8_t *)"Control-Read-Encryption-Key",
                     27,
                     p->write_key,
                     32));
    TRY(mbedtls_hkdf(md,
                     (const uint8_t *)"Control-Salt",
                     12,
                     key,
                     64,
                     (const uint8_t *)"Control-Write-Encryption-Key",
                     28,
                     p->read_key,
                     32));
    memcpy(p->shared_secret, key, 32);
    p->established = 1;
    result = 0;
done:
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(secret_bytes, sizeof(secret_bytes));
    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(expected, sizeof(expected));
    mbedtls_mpi_free(&A);
    mbedtls_mpi_free(&u);
    mbedtls_mpi_free(&temp);
    mbedtls_mpi_free(&secret);
    return result;
}

int pairing_setup(
    pairing_t *p, const uint8_t *in, size_t size, uint8_t *out, size_t capacity, size_t *out_size)
{
    uint8_t state, flags = 0, method = 0;
    size_t n, used = 0;
    if (!p || !in || !out || !out_size || capacity < 6)
        return -1;
    *out_size = 0;
    if (tlv_get(in, size, 6, &state, 1, &n) || n != 1 || p->established)
        return -1;
    int ok = 0;
    if (state == 1)
    {
        if (!tlv_get(in, size, 0, &method, 1, &n) && n == 1 && method == 0 &&
            !tlv_get(in, size, 0x13, &flags, 1, &n) && n == 1 && (flags & 0x10) && !begin(p))
        {
            srp_t *s = p->srp;
            uint8_t next = 2;
            ok = !tlv_put(out, capacity, &used, 6, &next, 1) &&
                 !tlv_put(out, capacity, &used, 2, s->salt, 16) &&
                 !tlv_put(out, capacity, &used, 3, s->public_key, SRP_BYTES);
        }
    }
    else if (state == 3)
    {
        uint8_t a[SRP_BYTES], proof[64], answer[64], next = 4;
        size_t a_size, proof_size;
        if (!tlv_get(in, size, 3, a, sizeof(a), &a_size) &&
            !tlv_get(in, size, 4, proof, sizeof(proof), &proof_size) && proof_size == 64 &&
            !verify(p, a, a_size, proof, answer))
            ok = !tlv_put(out, capacity, &used, 6, &next, 1) &&
                 !tlv_put(out, capacity, &used, 4, answer, 64);
        free_srp(p);
    }
    if (!ok)
    {
        pairing_close(p);
        uint8_t next = state == 3 ? 4 : 2, error = 2;
        used = 0;
        if (tlv_put(out, capacity, &used, 6, &next, 1) ||
            tlv_put(out, capacity, &used, 7, &error, 1))
            return -1;
    }
    *out_size = used;
    return 0;
}

static void nonce_bytes(uint8_t nonce[12], uint64_t counter)
{
    memset(nonce, 0, 12);
    for (unsigned i = 0; i < 8; ++i)
        nonce[4 + i] = (uint8_t)(counter >> (i * 8));
}
int pairing_seal(pairing_t *p, const uint8_t *in, size_t size, uint8_t *record)
{
    if (!p || !p->established || !in || !size || size > PAIR_RECORD_MAX || !record ||
        p->write_counter == UINT64_MAX)
        return -1;
    uint8_t nonce[12];
    nonce_bytes(nonce, p->write_counter);
    record[0] = (uint8_t)size;
    record[1] = (uint8_t)(size >> 8);
    mbedtls_chachapoly_context c;
    mbedtls_chachapoly_init(&c);
    int result = mbedtls_chachapoly_setkey(&c, p->write_key);
    if (!result)
        result = mbedtls_chachapoly_encrypt_and_tag(
            &c, size, nonce, record, 2, in, record + 2, record + 2 + size);
    mbedtls_chachapoly_free(&c);
    if (result)
        return -1;
    ++p->write_counter;
    return (int)size + 18;
}
int pairing_open(pairing_t *p, const uint8_t *record, size_t size, uint8_t *out)
{
    if (!p || !p->established || !record || !out || size < 18 || p->read_counter == UINT64_MAX)
        return -1;
    size_t n = record[0] | (size_t)record[1] << 8;
    if (!n || n > PAIR_RECORD_MAX || size != n + 18)
        return -1;
    uint8_t nonce[12];
    nonce_bytes(nonce, p->read_counter);
    mbedtls_chachapoly_context c;
    mbedtls_chachapoly_init(&c);
    int result = mbedtls_chachapoly_setkey(&c, p->read_key);
    if (!result)
        result = mbedtls_chachapoly_auth_decrypt(
            &c, n, nonce, record, 2, record + 2 + n, record + 2, out);
    mbedtls_chachapoly_free(&c);
    if (result)
        return -1;
    ++p->read_counter;
    return (int)n;
}
