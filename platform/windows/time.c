#include "../../include/platform_if.h"
#include <stdint.h>
#include <windows.h>

uint32_t win_get_time_ms(void)
{
    return (uint32_t)GetTickCount();
}

void *win_malloc(size_t sz)
{
    return malloc(sz);
}

void win_free(void *p)
{
    free(p);
}

const raop_platform_if_t win_platform_if = {
    .get_time_ms = win_get_time_ms,
    .malloc_fn = win_malloc,
    .free_fn = win_free,
};
