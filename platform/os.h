




#ifndef AIRPLAY_OS_H
#define AIRPLAY_OS_H

#include <stdint.h>

/* Minimal operating-system interface used by shared code for locking, waiting,
 * and reading a common UTC clock. */

/* Opaque native mutex storage initialized before service tasks start. */
typedef struct os_mutex
{
    void *handle;
} os_mutex_t;

/* Allocate the native mutex represented by mutex. */
/**
 * @brief os_mutex_init.
 * @param mutex Parameter named mutex.
 * @return Function result.
 */
int os_mutex_init(os_mutex_t *mutex);
/* Acquire mutex, blocking until ownership is available. */
/**
 * @brief os_mutex_lock.
 * @param mutex Parameter named mutex.
 */
void os_mutex_lock(os_mutex_t *mutex);
/* Release a mutex held by the current thread or task. */
/**
 * @brief os_mutex_unlock.
 * @param mutex Parameter named mutex.
 */
void os_mutex_unlock(os_mutex_t *mutex);
/* Release native mutex resources after all users have stopped. */
/**
 * @brief os_mutex_deinit.
 * @param mutex Parameter named mutex.
 */
void os_mutex_deinit(os_mutex_t *mutex);
/* Suspend the current thread or task for at least ms milliseconds. */
/**
 * @brief os_sleep_ms.
 * @param ms Parameter named ms.
 */
void os_sleep_ms(unsigned ms);
/* UTC microseconds since 1970. Embedded targets supply this from the board clock. */
/**
 * @brief os_time_us.
 * @return Function result.
 */
uint64_t os_time_us(void);

#endif
