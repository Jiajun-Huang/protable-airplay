#ifndef AIRPLAY_CRYPTO_MEMORY_H
#define AIRPLAY_CRYPTO_MEMORY_H

/* Configure mbedTLS to allocate exclusively from the fixed project-owned pool. */
int crypto_memory_init(void);
/* Release allocator bookkeeping after every mbedTLS object has been freed. */
void crypto_memory_deinit(void);

#endif
