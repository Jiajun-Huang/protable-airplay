#ifndef AIRPLAY_DISCOVERY_H
#define AIRPLAY_DISCOVERY_H

#include "protocol/mdns.h"
#include <stddef.h>

/* AirPlay discovery service. It owns the shared mDNS socket and advertises
 * both the AirPlay 1
 * RAOP record and the AirPlay 2 service record. */

/* Caller-owned discovery state and storage for stable DNS-SD record strings. */
typedef struct
{
    net_socket_t socket;
    mdns_instance_t service;
    mdns_instance_t airplay_service;
    char airplay_name[64], deviceid[32];
    const char *airplay_txt[8];
    char raop_name[96];
    char hostname[96];
    char local_ip[16];
    int initialized;
} airplay_discovery_t;

/* TXT arrays and strings must outlive discovery. Names and IPv4 are copied. */
int airplay_discovery_init(airplay_discovery_t *discovery,
                           const char *friendly_name,
                           const char *local_mac,
                           const char *local_ip,
                           const char **raop_txt_entries,
                           size_t raop_txt_count);

/* Receive and answer one batch of mDNS traffic. Returns zero on idle or success. */
int airplay_discovery_poll(airplay_discovery_t *discovery, int timeout_ms);
/* Send goodbye records when possible and release the mDNS socket. */
void airplay_discovery_deinit(airplay_discovery_t *discovery);

#endif
