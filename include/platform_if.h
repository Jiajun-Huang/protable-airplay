#ifndef PLATFORM_IF_H
#define PLATFORM_IF_H

#include <stdint.h>
#include <stddef.h>

// Network interface callbacks provided by platform layer

typedef struct
{
    void (*tcp_send)(const uint8_t *, size_t);
    void (*udp_send)(const uint8_t *, size_t);
} raop_net_if_t;

// Audio interface callbacks provided by platform layer

typedef struct
{
    void (*play_pcm)(int16_t *samples, size_t frames);
} raop_audio_if_t;

// Platform utilities (time, memory)

typedef struct
{
    uint32_t (*get_time_ms)(void);
    void *(*malloc_fn)(size_t);
    void (*free_fn)(void *);
} raop_platform_if_t;

// mDNS system information interface provided by platform layer

typedef struct
{
    void (*get_local_ipv4)(char *buf, size_t bufsize);
    void (*get_hostname)(char *buf, size_t bufsize);
    void (*get_mac_hex)(char *buf, size_t bufsize);
    int (*get_ipv6_addresses)(char **addrs, int max_addrs);
} mdns_system_info_t;

#endif // PLATFORM_IF_H
