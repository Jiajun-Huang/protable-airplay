


#ifndef AIRPLAY_PAIRING_H
#define AIRPLAY_PAIRING_H
#include <stddef.h>
#include <stdint.h>

#define PAIR_RECORD_MAX          1024
#define PAIRING_SRP_STORAGE_SIZE 512

/* Transient AirPlay 2 pairing and encrypted control-record transport. */

/* Aligned, fixed storage for the private SRP state. */
typedef union
{
    max_align_t alignment;
    uint8_t bytes[PAIRING_SRP_STORAGE_SIZE];
} pairing_srp_storage_t;

/* Per-connection pairing keys, counters, and temporary SRP state. */
typedef struct
{
    pairing_srp_storage_t srp_storage;
    void *srp;
    uint8_t read_key[32], write_key[32], shared_secret[32];
    uint64_t read_counter, write_counter;
    int established;
} pairing_t;

/* Transient Pair-Setup (SRP-6a/SHA-512); caller zero-initializes the context. */
/**
 * @brief pairing_setup.
 * @param pair Parameter named pair.
 * @param input Parameter named input.
 * @param input_size Parameter named input_size.
 * @param output Parameter named output.
 * @param capacity Parameter named capacity.
 * @param output_size Parameter named output_size.
 * @return Function result.
 */
int pairing_setup(pairing_t *pair,
                  const uint8_t *input,
                  size_t input_size,
                  uint8_t *output,
                  size_t capacity,
                  size_t *output_size);
/* Release temporary SRP state and erase all established session keys. */
/**
 * @brief pairing_close.
 * @param pair Parameter named pair.
 */
void pairing_close(pairing_t *pair);
/* Seal plaintext as a length-prefixed ChaCha20-Poly1305 control record. */
/**
 * @brief pairing_seal.
 * @param pair Parameter named pair.
 * @param input Parameter named input.
 * @param size Parameter named size.
 * @param record Parameter named record.
 * @return Function result.
 */
int pairing_seal(pairing_t *pair, const uint8_t *input, size_t size, uint8_t *record);
/* Authenticate and decrypt one length-prefixed control record. */
/**
 * @brief pairing_open.
 * @param pair Parameter named pair.
 * @param record Parameter named record.
 * @param size Parameter named size.
 * @param output Parameter named output.
 * @return Function result.
 */
int pairing_open(pairing_t *pair, const uint8_t *record, size_t size, uint8_t *output);
#endif
