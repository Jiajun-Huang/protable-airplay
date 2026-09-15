


#ifndef AIRPLAY_NETWORK_UTIL_H
#define AIRPLAY_NETWORK_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Allocation-free parsing helpers shared by wire-protocol modules. */

/* Parse dotted-decimal IPv4 into network-order bytes in uint32_t storage. */
/**
 * @brief net_str_to_ipv4.
 * @param ip Parameter named ip.
 * @param address Parameter named address.
 * @return Function result.
 */
int net_str_to_ipv4(const char *ip, uint32_t *address);
/* Format network-order IPv4 storage as dotted-decimal text. */
/**
 * @brief net_ipv4_to_str.
 * @param address Parameter named address.
 * @param text Parameter named text.
 * @param capacity Parameter named capacity.
 * @return Function result.
 */
int net_ipv4_to_str(uint32_t address, char *text, size_t capacity);
/* Compare two ASCII strings without locale-dependent case folding. */
/**
 * @brief net_ascii_casecmp.
 * @param a Parameter named a.
 * @param b Parameter named b.
 * @return Function result.
 */
int net_ascii_casecmp(const char *a, const char *b);
/* Compare at most count ASCII bytes without locale-dependent case folding. */
/**
 * @brief net_ascii_ncasecmp.
 * @param a Parameter named a.
 * @param b Parameter named b.
 * @param count Parameter named count.
 * @return Function result.
 */
int net_ascii_ncasecmp(const char *a, const char *b, size_t count);

#endif
