#ifndef AIRPLAY_PTP_SYNC_H
#define AIRPLAY_PTP_SYNC_H
#include "net.h"

/* AirPlay 2 PTP listener and RTP playback-anchor mapper. */

/* Sender-provided relationship between an RTP timestamp and PTP network time. */
typedef struct
{
    uint64_t clock_id, network_us;
    uint32_t rtp_time;
    int valid, playing;
} airplay_anchor_t;

/* PTP clock identity, offset samples, and event/general UDP sockets. */
typedef struct
{
    net_socket_t sockets[2];
    uint64_t clock_id, sync_clock, sync_received_us, updated_us;
    int64_t offset_us, sync_correction;
    uint16_t sequence;
    uint8_t domain, source_port[10];
    int pending, ready, opened;
} ptp_sync_t;

/**
 * @brief Initialize an unopened PTP synchronizer
 *
 * @param p PTP synchronizer state
 */
void ptp_sync_init(ptp_sync_t *p);

/**
 * @brief Open PTP event and general-message multicast sockets
 *
 * @param p PTP synchronizer state
 * @param local_ip Local IPv4 address used to join the multicast group
 * @return 0 on success, -1 on socket or multicast setup failure
 */
int ptp_sync_open(ptp_sync_t *p, const char *local_ip);

/**
 * @brief Close PTP sockets and reset synchronizer state
 *
 * @param p PTP synchronizer state
 */
void ptp_sync_close(ptp_sync_t *p);

/**
 * @brief Select the sender clock used for synchronization
 *
 * @param p PTP synchronizer state
 * @param clock_id PTP clock identity from the AirPlay anchor
 */
void ptp_sync_set_clock(ptp_sync_t *p, uint64_t clock_id);

/**
 * @brief Consume one PTP Sync or Follow_Up packet
 *
 * @param p PTP synchronizer state
 * @param data PTP packet bytes
 * @param size Number of bytes in the packet
 * @param received_us Local receive time in microseconds
 * @return 1 when a synchronization sample is accepted, 0 when ignored, -1 when invalid
 */
int ptp_sync_packet(ptp_sync_t *p, const uint8_t *data, size_t size, uint64_t received_us);

/**
 * @brief Drain pending PTP packets without blocking
 *
 * @param p PTP synchronizer state
 * @return 0 on success, -1 on socket receive failure
 */
int ptp_sync_poll(ptp_sync_t *p);

/**
 * @brief Parse an AirPlay RTP-to-PTP playback anchor
 *
 * @param anchor Destination playback anchor
 * @param data AirPlay control packet bytes
 * @param size Number of bytes in the control packet
 * @return 0 on success, -1 when the packet is invalid
 */
int ptp_sync_anchor(airplay_anchor_t *anchor, const uint8_t *data, size_t size);

/**
 * @brief Convert an RTP timestamp into a local playback deadline
 *
 * @param p PTP synchronizer state
 * @param anchor AirPlay RTP-to-PTP playback anchor
 * @param timestamp RTP timestamp to schedule
 * @param rate Audio sample rate in frames per second
 * @param latency Additional sender playout latency in frames
 * @param deadline Destination local deadline in microseconds
 * @return 0 on success, -1 when synchronization data is unavailable or invalid
 */
int ptp_sync_deadline(const ptp_sync_t *p,
                      const airplay_anchor_t *anchor,
                      uint32_t timestamp,
                      uint32_t rate,
                      uint32_t latency,
                      uint64_t *deadline);
#endif
