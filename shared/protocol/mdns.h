#ifndef AIRPLAY_MDNS_H
#define AIRPLAY_MDNS_H

#include <stddef.h>
#include <stdint.h>
#include "net.h"

#define MDNS_MCAST_ADDR "224.0.0.251"
#define MDNS_PORT 5353

typedef enum
{
    MDNS_OK = 0,
    MDNS_ERR_INVALID_ARGS = -1,
    MDNS_ERR_BUFFER_OVERFLOW = -2,
    MDNS_ERR_SOCKET_ERROR = -3,
    MDNS_ERR_INVALID_DATA = -4
} mdns_error_t;

typedef struct
{
    const char *service_type;
    const char *instance_name;
    const char *hostname;
    uint16_t port;
    const char *ipv4;
    const char **txt_entries;
    uint16_t txt_count;
} mdns_config_t;

typedef struct
{
    mdns_config_t config;
    net_socket_t *socket;
} mdns_instance_t;

/* Config strings and socket remain owned by the caller and must outlive instance. */
mdns_error_t mdns_create(mdns_instance_t *instance, const mdns_config_t *config, net_socket_t *socket);
mdns_error_t mdns_handle_packet(mdns_instance_t *instance, const uint8_t *data, size_t length);
mdns_error_t mdns_handle_packet_from(mdns_instance_t *instance, const uint8_t *data,
                                     size_t length, const net_addr_t *source);
mdns_error_t mdns_announce(mdns_instance_t *instance);
mdns_error_t mdns_goodbye(mdns_instance_t *instance);

#endif
