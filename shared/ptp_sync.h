#ifndef AIRPLAY_PTP_SYNC_H
#define AIRPLAY_PTP_SYNC_H
#include "net.h"
typedef struct {
    uint64_t clock_id, network_us;
    uint32_t rtp_time;
    int valid, playing;
} airplay_anchor_t;
typedef struct {
    net_socket_t sockets[2];
    uint64_t clock_id, sync_clock, sync_received_us, updated_us;
    int64_t offset_us, sync_correction;
    uint16_t sequence;
    uint8_t domain, source_port[10];
    int pending, ready, opened;
} ptp_sync_t;
void ptp_sync_init(ptp_sync_t *p);
int ptp_sync_open(ptp_sync_t *p, const char *local_ip);
void ptp_sync_close(ptp_sync_t *p);
void ptp_sync_set_clock(ptp_sync_t *p, uint64_t clock_id);
int ptp_sync_packet(ptp_sync_t *p, const uint8_t *data, size_t size, uint64_t received_us);
int ptp_sync_poll(ptp_sync_t *p);
/* AirPlay realtime control packet: RTP at byte 4, PTP nanoseconds at 8,
 * sender's current RTP at 16, and clock identity at 20. */
int ptp_sync_anchor(airplay_anchor_t *anchor, const uint8_t *data, size_t size);
int ptp_sync_deadline(const ptp_sync_t *p, const airplay_anchor_t *anchor,
                      uint32_t timestamp, uint32_t rate, uint32_t latency, uint64_t *deadline);
#endif
