#include "crypto/crypto_memory.h"

#include "airplay_config.h"

#include <stddef.h>
#include <stdatomic.h>

#include <mbedtls/memory_buffer_alloc.h>
#include <mbedtls/threading.h>

typedef union
{
    max_align_t alignment;
    unsigned char bytes[AIRPLAY_CRYPTO_MEMORY_SIZE];
} crypto_memory_pool_t;

static crypto_memory_pool_t pool;
static int initialized;

/**
 * @brief Initialize an mbedTLS mutex callback object
 *
 * @param mutex Mutex callback object
 */
static void mutex_init(mbedtls_threading_mutex_t *mutex)
{
    atomic_init(&mutex->locked, 0);
    mutex->valid = 1;
}

/**
 * @brief Mark an mbedTLS mutex callback object as unavailable
 *
 * @param mutex Mutex callback object
 */
static void mutex_free(mbedtls_threading_mutex_t *mutex)
{
    mutex->valid = 0;
}

/**
 * @brief Lock an mbedTLS mutex callback object
 *
 * @param mutex Mutex callback object
 * @return 0 on success, or an mbedTLS mutex error when invalid
 */
static int mutex_lock(mbedtls_threading_mutex_t *mutex)
{
    if (!mutex->valid)
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    while (atomic_exchange_explicit(&mutex->locked, 1, memory_order_acquire))
    {
    }
    return 0;
}

/**
 * @brief Unlock an mbedTLS mutex callback object
 *
 * @param mutex Mutex callback object
 * @return 0 on success, or an mbedTLS mutex error when invalid
 */
static int mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
    if (!mutex->valid)
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    atomic_store_explicit(&mutex->locked, 0, memory_order_release);
    return 0;
}

int crypto_memory_init(void)
{
    if (initialized)
        return 0;
    mbedtls_threading_set_alt(mutex_init, mutex_free, mutex_lock, mutex_unlock);
    mbedtls_memory_buffer_alloc_init(pool.bytes, sizeof(pool.bytes));
    initialized = 1;
    return 0;
}

void crypto_memory_deinit(void)
{
    if (!initialized)
        return;
    mbedtls_memory_buffer_alloc_free();
    mbedtls_threading_free_alt();
    initialized = 0;
}
