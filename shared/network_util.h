#ifndef NETWORK_UTIL_H
#define NETWORK_UTIL_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Network utility functions for IP address conversion
 * Provides cross-platform conversion between string and network byte order
 */

/**
 * @brief Convert IPv4 string to network byte order uint32_t
 * Cross-platform wrapper around inet_pton/InetPton
 *
 * @param ip_str IPv4 address string (e.g., "192.168.1.1")
 * @param out_addr Pointer to output uint32_t (network byte order)
 * @return 0 on success, negative on error
 */
int net_str_to_ipv4(const char *ip_str, uint32_t *out_addr);

/**
 * @brief Convert network byte order uint32_t to IPv4 string
 * Cross-platform wrapper around inet_ntop/InetNtop
 *
 * @param addr IPv4 address (network byte order)
 * @param out_str Output buffer for IP string
 * @param str_len Buffer size (minimum 16 bytes for IPv4)
 * @return 0 on success, negative on error
 */
int net_ipv4_to_str(uint32_t addr, char *out_str, size_t str_len);

/**
 * @brief Detect the primary local IPv4 address
 * Uses UDP connect trick to determine outbound interface
 *
 * @param out_str Output buffer for IP string
 * @param str_len Buffer size (minimum 16 bytes)
 * @return 0 on success, negative on error
 */
int net_get_local_ipv4(char *out_str, size_t str_len);

/**
 * @brief mDNS multicast address constant (224.0.0.251)
 */
#define NET_MDNS_MCAST_ADDR "224.0.0.251"
#define NET_MDNS_PORT 5353

#endif // NETWORK_UTIL_H
