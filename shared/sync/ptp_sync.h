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

/* Initialize an unopened PTP synchronizer. */
void ptp_sync_init(ptp_sync_t *p);
/* Join the PTP multicast groups on the selected local IPv4 interface. */
int ptp_sync_open(ptp_sync_t *p, const char *local_ip);
/* Close PTP sockets and clear clock readiness. */
void ptp_sync_close(ptp_sync_t *p);
/* Select the sender clock identity required by future samples and anchors. */
void ptp_sync_set_clock(ptp_sync_t *p, uint64_t clock_id);
/* Consume one PTP event or general message received at received_us. */
int ptp_sync_packet(ptp_sync_t *p, const uint8_t *data, size_t size, uint64_t received_us);
/* Drain ready PTP packets without blocking. */
int ptp_sync_poll(ptp_sync_t *p);
/* AirPlay realtime control packet: RTP at byte 4, PTP nanoseconds at 8,
 * sender's current RTP at 16, and clock identity at 20. */
int ptp_sync_anchor(airplay_anchor_t *anchor, const uint8_t *data, size_t size);
/* Convert one RTP timestamp and sender anchor to a local playback deadline. */
int ptp_sync_deadline(const ptp_sync_t *p,
                      const airplay_anchor_t *anchor,
                      uint32_t timestamp,
                      uint32_t rate,
                      uint32_t latency,
                      uint64_t *deadline);
#endif
