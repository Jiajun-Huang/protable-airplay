#include "os.h"

#include <windows.h>
#include <stdlib.h>

int os_mutex_init(os_mutex_t *mutex)
{
    CRITICAL_SECTION *native;
    if (!mutex)
        return -1;
    mutex->handle = NULL;
    native = (CRITICAL_SECTION *)malloc(sizeof(*native));
    if (!native)
        return -1;
    if (!InitializeCriticalSectionAndSpinCount(native, 0))
    {
        free(native);
        return -1;
    }
    mutex->handle = native;
    return 0;
}

void os_mutex_lock(os_mutex_t *mutex)
{
    EnterCriticalSection((CRITICAL_SECTION *)mutex->handle);
}

void os_mutex_unlock(os_mutex_t *mutex)
{
    LeaveCriticalSection((CRITICAL_SECTION *)mutex->handle);
}

void os_mutex_deinit(os_mutex_t *mutex)
{
    if (mutex && mutex->handle)
    {
        DeleteCriticalSection((CRITICAL_SECTION *)mutex->handle);
        free(mutex->handle);
        mutex->handle = NULL;
    }
}

void os_sleep_ms(unsigned ms)
{
    Sleep(ms);
}

uint64_t os_time_us(void)
{
    FILETIME time;
    ULARGE_INTEGER ticks;
    GetSystemTimePreciseAsFileTime(&time);
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    return (ticks.QuadPart - UINT64_C(116444736000000000)) / 10;
}
