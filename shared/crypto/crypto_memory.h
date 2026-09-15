
#ifndef AIRPLAY_CRYPTO_MEMORY_H
#define AIRPLAY_CRYPTO_MEMORY_H

/**
 * @brief Initialize the fixed mbedTLS memory pool and mutex callbacks.
 * @return 0 on success, or a negative value on failure.
 */
int crypto_memory_init(void);

/**
 * @brief Release allocator bookkeeping after every mbedTLS object has been freed.
 */
void crypto_memory_deinit(void);

#endif
