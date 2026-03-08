#ifndef AIRPLAY_DISCOVERY_H
#define AIRPLAY_DISCOVERY_H

#include <stddef.h>

#include "udp_if.h"
#include "mdns.h"

/**
 * Initialize dual-service mDNS discovery (RAOP + AirPlay) using one UDP socket.
 * Returns 0 on success, negative on failure.
 */
int airplay_discovery_init(const char *friendly_name,
                           const char *local_mac,
                           const char *local_ip,
                           const char **raop_txt_entries,
                           size_t raop_txt_count,
                           const char **airplay_txt_entries,
                           size_t airplay_txt_count);

/**
 * Run the discovery receive/respond loop until stop_fn returns non-zero.
 */
void airplay_discovery_run(int (*stop_fn)(void));

/**
 * Send goodbye records and close socket.
 */
void airplay_discovery_deinit(void);

#endif // AIRPLAY_DISCOVERY_H