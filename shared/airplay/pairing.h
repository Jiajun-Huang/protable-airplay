#ifndef AIRPLAY_PAIRING_H
#define AIRPLAY_PAIRING_H
#include <stddef.h>
#include <stdint.h>

#define PAIR_RECORD_MAX 1024

/* Transient AirPlay 2 pairing and encrypted control-record transport. */

/* Per-connection pairing keys, counters, and temporary SRP state. */
typedef struct
{
    void *srp;
    uint8_t read_key[32], write_key[32], shared_secret[32];
    uint64_t read_counter, write_counter;
    int established;
} pairing_t;

/* Transient Pair-Setup (SRP-6a/SHA-512); caller zero-initializes the context. */
int pairing_setup(pairing_t *pair,
                  const uint8_t *input,
                  size_t input_size,
                  uint8_t *output,
                  size_t capacity,
                  size_t *output_size);
/* Release temporary SRP state and erase all established session keys. */
void pairing_close(pairing_t *pair);
/* Seal plaintext as a length-prefixed ChaCha20-Poly1305 control record. */
int pairing_seal(pairing_t *pair, const uint8_t *input, size_t size, uint8_t *record);
/* Authenticate and decrypt one length-prefixed control record. */
int pairing_open(pairing_t *pair, const uint8_t *record, size_t size, uint8_t *output);
#endif
