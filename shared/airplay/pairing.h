#ifndef AIRPLAY_PAIRING_H
#define AIRPLAY_PAIRING_H
#include <stddef.h>
#include <stdint.h>

#define PAIR_RECORD_MAX 1024
typedef struct {
    void *srp;
    uint8_t read_key[32], write_key[32], shared_secret[32];
    uint64_t read_counter, write_counter;
    int established;
} pairing_t;

/* Transient Pair-Setup (SRP-6a/SHA-512); caller zero-initializes the context. */
int pairing_setup(pairing_t *pair, const uint8_t *input, size_t input_size,
                  uint8_t *output, size_t capacity, size_t *output_size);
void pairing_close(pairing_t *pair);
/* Control records: little-endian length, ciphertext, Poly1305 tag. */
int pairing_seal(pairing_t *pair, const uint8_t *input, size_t size, uint8_t *record);
int pairing_open(pairing_t *pair, const uint8_t *record, size_t size, uint8_t *output);
#endif
