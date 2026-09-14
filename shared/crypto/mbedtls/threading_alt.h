#ifndef AIRPLAY_MBEDTLS_THREADING_ALT_H
#define AIRPLAY_MBEDTLS_THREADING_ALT_H

#include <stdatomic.h>

/* mbedTLS alternate mutex backed by the project's platform lock interface. */
typedef struct mbedtls_threading_mutex_t
{
    atomic_bool locked;
    int valid;
} mbedtls_threading_mutex_t;

#endif
