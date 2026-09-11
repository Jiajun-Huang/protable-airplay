#ifndef AIRPLAY_NETWORK_UTIL_H
#define AIRPLAY_NETWORK_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* IPv4 addresses use network-order bytes in the uint32_t storage. */
int net_str_to_ipv4(const char *ip, uint32_t *address);
int net_ipv4_to_str(uint32_t address, char *text, size_t capacity);
int net_ascii_casecmp(const char *a, const char *b);
int net_ascii_ncasecmp(const char *a, const char *b, size_t count);

#endif
