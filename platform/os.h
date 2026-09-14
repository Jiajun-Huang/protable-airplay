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
int os_mutex_init(os_mutex_t *mutex);
/* Acquire mutex, blocking until ownership is available. */
void os_mutex_lock(os_mutex_t *mutex);
/* Release a mutex held by the current thread or task. */
void os_mutex_unlock(os_mutex_t *mutex);
/* Release native mutex resources after all users have stopped. */
void os_mutex_deinit(os_mutex_t *mutex);
/* Suspend the current thread or task for at least ms milliseconds. */
void os_sleep_ms(unsigned ms);
/* UTC microseconds since 1970. Embedded targets supply this from the board clock. */
uint64_t os_time_us(void);

#endif
