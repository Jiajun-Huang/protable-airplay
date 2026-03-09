#ifndef PIPELINE_H
#define PIPELINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char local_ip[16];
    char local_mac_hex[13];
    char local_mac_colon[18];
    char device_name[64];
} pipeline_identity_t;

typedef struct
{
    pipeline_identity_t identity;
} pipeline_startup_t;

int pipeline_start(const pipeline_startup_t *startup);

#ifdef __cplusplus
}
#endif

#endif // PIPELINE_H
