#ifndef AIRPLAY_OS_H
#define AIRPLAY_OS_H

#include <stdint.h>

/* Native mutex storage belongs to the platform. Initialize before starting tasks. */
typedef struct os_mutex { void *handle; } os_mutex_t;

int os_mutex_init(os_mutex_t *mutex);
void os_mutex_lock(os_mutex_t *mutex);
void os_mutex_unlock(os_mutex_t *mutex);
void os_mutex_deinit(os_mutex_t *mutex);
void os_sleep_ms(unsigned ms);
/* UTC microseconds since 1970. Embedded targets supply this from the board clock. */
uint64_t os_time_us(void);

#endif
