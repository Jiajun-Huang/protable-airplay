#ifndef AIRPLAY_MDNS_H
#define AIRPLAY_MDNS_H

#include "net.h"
#include <stddef.h>
#include <stdint.h>

#define MDNS_MCAST_ADDR "224.0.0.251"
#define MDNS_PORT       5353

/* Minimal authoritative mDNS/DNS-SD responder for AirPlay service records. */

/* Stable result codes returned by every mDNS operation. */
typedef enum
{
    MDNS_OK = 0,
    MDNS_ERR_INVALID_ARGS = -1,
    MDNS_ERR_BUFFER_OVERFLOW = -2,
    MDNS_ERR_SOCKET_ERROR = -3,
    MDNS_ERR_INVALID_DATA = -4
} mdns_error_t;

/* Caller-owned DNS-SD record description; referenced strings must remain valid. */
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

/* One advertised service bound to a shared multicast UDP socket. */
typedef struct
{
    mdns_config_t config;
    net_socket_t *socket;
} mdns_instance_t;

/* Config strings and socket remain owned by the caller and must outlive instance. */
mdns_error_t mdns_create(mdns_instance_t *instance,
                         const mdns_config_t *config,
                         net_socket_t *socket);
/* Parse and answer one packet when no source endpoint filtering is required. */
mdns_error_t mdns_handle_packet(mdns_instance_t *instance, const uint8_t *data, size_t length);
/* Parse one packet and select multicast or unicast response from its source. */
mdns_error_t mdns_handle_packet_from(mdns_instance_t *instance,
                                     const uint8_t *data,
                                     size_t length,
                                     const net_addr_t *source);
/* Broadcast the service's PTR, SRV, TXT, and address records. */
mdns_error_t mdns_announce(mdns_instance_t *instance);
/* Broadcast zero-TTL records so peers remove the service promptly. */
mdns_error_t mdns_goodbye(mdns_instance_t *instance);

#endif
