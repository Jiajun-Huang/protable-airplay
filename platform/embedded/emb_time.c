#include <stdint.h>

// Provide simple time and allocator for embedded

uint32_t emb_get_time_ms(void)
{
    // TBD: use system tick
    return 0;
}

void *emb_malloc(size_t sz)
{
    (void)sz;
    return NULL;
}

void emb_free(void *p)
{
    (void)p;
}

const raop_platform_if_t emb_platform_if = {
    .get_time_ms = emb_get_time_ms,
    .malloc_fn = emb_malloc,
    .free_fn = emb_free,
};
