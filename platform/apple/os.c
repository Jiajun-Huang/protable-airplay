#define _POSIX_C_SOURCE 200809L
#include "os.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

int os_mutex_init(os_mutex_t *mutex)
{
    pthread_mutex_t *native;
    if (!mutex)
        return -1;
    mutex->handle = NULL;
    native = (pthread_mutex_t *)malloc(sizeof(*native));
    if (!native)
        return -1;
    if (pthread_mutex_init(native, NULL) != 0)
    {
        free(native);
        return -1;
    }
    mutex->handle = native;
    return 0;
}

void os_mutex_lock(os_mutex_t *mutex)
{
    pthread_mutex_lock((pthread_mutex_t *)mutex->handle);
}

void os_mutex_unlock(os_mutex_t *mutex)
{
    pthread_mutex_unlock((pthread_mutex_t *)mutex->handle);
}

void os_mutex_deinit(os_mutex_t *mutex)
{
    if (mutex && mutex->handle)
    {
        pthread_mutex_destroy((pthread_mutex_t *)mutex->handle);
        free(mutex->handle);
        mutex->handle = NULL;
    }
}

void os_sleep_ms(unsigned ms)
{
    struct timespec remaining;
    remaining.tv_sec = ms / 1000;
    remaining.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR)
    {
    }
}

uint64_t os_time_us(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_REALTIME, &time) != 0)
        return 0;
    return (uint64_t)time.tv_sec * UINT64_C(1000000) + (uint64_t)time.tv_nsec / 1000;
}
