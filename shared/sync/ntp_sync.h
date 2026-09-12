#ifndef NTP_SYNC_H
#define NTP_SYNC_H
#include <stddef.h>
#include <stdint.h>

/* AirPlay 1 clock synchronizer. It combines RAOP timing exchanges with RTP/NTP
 * sync anchors to map sender timestamps onto the local UTC clock. */

/* Unsigned NTP seconds and 32-bit fractional seconds. */
typedef struct
{
    uint32_t seconds, fraction;
} ntp_timestamp_t;
/* One audio thread owns timing state. Offset is sender clock minus local clock. */
typedef struct ntp_sync
{
    int64_t clock_offset_us, rtt_us;
    uint32_t rtp_base, latency_frames;
    ntp_timestamp_t ntp_base;
    ntp_timestamp_t request_time;
    uint64_t request_local_us, next_request_us, best_sample_us;
    unsigned requests, samples;
    int synchronized, anchor_valid, request_pending;
} ntp_sync_t;

/* Reset synchronization state and schedule the first timing request. */
int ntp_sync_init(ntp_sync_t *sync);
/* Read the local UTC clock as an NTP timestamp. */
ntp_timestamp_t ntp_sync_now(void);
/* Return signed t2 minus t1 in microseconds, including NTP wrap handling. */
int64_t ntp_sync_diff_us(ntp_timestamp_t t1, ntp_timestamp_t t2);
/* Produce a 32-byte RAOP timing request when due; 1 produced, 0 not due. */
int ntp_sync_request(ntp_sync_t *sync, uint8_t packet[32]);
/* Consume one RAOP timing reply and update the best clock-offset sample. */
int ntp_sync_process_packet(ntp_sync_t *sync, const uint8_t *data, size_t len);
/* Build the timing reply used when the peer sends a request to this receiver. */
int ntp_sync_reply(const uint8_t *request, size_t len, uint8_t reply[32]);
/* Consume one RAOP RTP/NTP synchronization control packet. */
int ntp_sync_control(ntp_sync_t *sync, const uint8_t *data, size_t len, uint32_t rate);
/* 0 only when both a timing exchange and a sync anchor are available. */
int ntp_sync_deadline(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate, uint64_t *local_us);
/* Convert an RTP timestamp to the sender's NTP timeline. */
ntp_timestamp_t ntp_sync_rtp_to_ntp(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate);
/* Return the current sender-clock-minus-local-clock offset in microseconds. */
int64_t ntp_sync_get_offset_us(ntp_sync_t *sync);
/* Clear all clock samples and anchors. */
void ntp_sync_deinit(ntp_sync_t *sync);
#endif
