#ifndef AIRPLAY_DISCOVERY_H
#define AIRPLAY_DISCOVERY_H

#include <stddef.h>
#include "mdns.h"

typedef struct {
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

/* Receive once and dispatch to the audio service. Returns 0 on idle, -1 on I/O failure. */
int airplay_discovery_poll(airplay_discovery_t *discovery, int timeout_ms);
void airplay_discovery_deinit(airplay_discovery_t *discovery);

#endif
