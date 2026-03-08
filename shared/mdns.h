
#ifndef MDNS_H
#define MDNS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "udp_if.h"

// mDNS multicast constants
#define MDNS_MCAST_ADDR "224.0.0.251"
#define MDNS_PORT 5353

/**
 * DNS Message Format Structures
 * RFC 1035 - Domain Names - Implementation and Specification: https://tools.ietf.org/html/rfc1035
 * RFC 6762 - Multicast DNS: https://tools.ietf.org/html/rfc6762
 */

/**
 * @brief DNS Header (12 bytes)
 * RFC 1035 Section 4.1.1
 */
typedef struct __attribute__((packed))
{
    uint16_t id;      // Identifier
    uint16_t flags;   // Flags: QR(1) OPCODE(4) AA(1) TC(1) RD(1) RA(1) Z(3) RCODE(4)
    uint16_t qdcount; // Question count
    uint16_t ancount; // Answer count
    uint16_t nscount; // Authority count
    uint16_t arcount; // Additional count
} dns_header_t;
/**
 * @brief DNS Question (variable length)
 * RFC 1035 Section 4.1.2
 * Format: <QNAME><QTYPE><QCLASS>
 */
typedef struct __attribute__((packed))
{
    // QNAME follows (variable length, encoded domain name)
    uint16_t qtype;  // Question type
    uint16_t qclass; // Question class
} dns_question_t;

/**
 * @brief DNS Resource Record Header
 * RFC 1035 Section 4.1.3
 * Format: <NAME><TYPE><CLASS><TTL><RDLENGTH><RDATA>
 */
typedef struct __attribute__((packed))
{
    // NAME follows (variable length, encoded domain name)
    uint16_t type;     // Resource type
    uint16_t class;    // Resource class
    uint32_t ttl;      // Time to live (in seconds)
    uint16_t rdlength; // RDATA length
    // RDATA follows (variable length, depends on type)
} dns_rr_t;

/**
 * @brief SRV Record RDATA (Service Record)
 * RFC 2782 - A DNS RR for specifying the location of services (SRV): https://tools.ietf.org/html/rfc2782
 * Used for AirPlay/RAOP service discovery
 */
typedef struct __attribute__((packed))
{
    uint16_t priority; // Priority (lower = higher priority)
    uint16_t weight;   // Weight (relative weight among same priority)
    uint16_t port;     // Port number (network byte order)
    // Target hostname follows (variable length, encoded domain name)
} dns_srv_rdata_t;

/**
 * @brief A Record RDATA (IPv4 Address Record)
 * RFC 1035 Section 3.4.1
 */
typedef struct __attribute__((packed))
{
    uint32_t addr; // IPv4 address (network byte order)
} dns_a_rdata_t;

/**
 * @brief Error codes for mDNS operations
 */
typedef enum
{
    MDNS_OK = 0,
    MDNS_ERR_INVALID_ARGS = -1,
    MDNS_ERR_BUFFER_OVERFLOW = -2,
    MDNS_ERR_SOCKET_ERROR = -3,
    MDNS_ERR_INVALID_DATA = -4,
    MDNS_ERR_MEMORY = -5
} mdns_error_t;

/**
 * @brief mDNS service configuration
 *
 * Defines the service to be announced via mDNS.
 * Example hierarchy:
 *   Service Type: _raop._tcp.local
 *        |
 *      Instance: MySpeaker._raop._tcp.local
 *        |
 *      Hostname: myspeaker.local -> 192.168.1.50
 */
typedef struct
{
    const char *service_type;  // "_raop._tcp.local"
    const char *instance_name; // "MySpeaker"
    const char *hostname;      // "myspeaker.local"
    uint16_t port;             // Service port
    const char *ipv4;          // IPv4 address string (e.g., "192.168.1.100")
    const char **txt_entries;  // TXT record entries (key=value format)
    uint16_t txt_count;        // Number of TXT entries
} mdns_config_t;

/**
 * @brief Internal structure for an mDNS instance
 */
typedef struct mdns_instance
{
    mdns_config_t config;
    udp_socket_t *udp_socket;
} mdns_instance_t;

/**
 * @brief Create and initialize an mDNS instance with the given configuration
 *
 * Platform layer owns thread and socket lifecycle.
 * @param config Configuration for the mDNS service to announce
 * @param udp_socket UDP socket used for send/receive
 * @param instance Caller-allocated mDNS instance
 * @return MDNS_OK on success, error code on failure
 */
mdns_error_t mdns_create(mdns_instance_t *instance, const mdns_config_t *config, udp_socket_t *udp_socket);

/**
 * @brief Handle an incoming mDNS packet (query or response)
 *
 * Platform layer should call this whenever it receives a packet on mDNS socket.
 * If query matches the configured service, mDNS response is sent via udp_socket.
 *
 * @param instance mDNS instance
 * @param data raw packet bytes
 * @param len packet length
 * @return MDNS_OK on success, error code on failure
 */
mdns_error_t mdns_handle_packet(mdns_instance_t *instance, const uint8_t *data, size_t len);

/**
 * @brief Send mDNS announcement packet
 */
mdns_error_t mdns_announce(mdns_instance_t *instance);

/**
 * @brief Send mDNS goodbye packet (TTL=0)
 */
mdns_error_t mdns_goodbye(mdns_instance_t *instance);

/**
 * @brief loop to receive mDNS packets and handle them with a stop condition
 *
 * This is a convenience function for platforms that want a simple blocking loop to handle mDNS.
 * The loop will run until the provided stop_fn returns true.
 * @param instance mDNS instance
 * @param stop_fn Function pointer that returns true when the loop should stop
 */
void mdns_run(mdns_instance_t *instance, int (*stop_fn)(void));

void mdns_run_multiple(mdns_instance_t **instances, size_t count, int (*stop_fn)(void));
#endif // MDNS_H