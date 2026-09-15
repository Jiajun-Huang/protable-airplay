









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

/**
 * @brief Reset NTP synchronization state
 *
 * @param sync NTP synchronizer state
 * @return 0 on success, -1 when sync is NULL
 */
int ntp_sync_init(ntp_sync_t *sync);

/**
 * @brief Read the local clock as an NTP timestamp
 *
 * @return Current local time in NTP format
 */
ntp_timestamp_t ntp_sync_now(void);

/**
 * @brief Calculate the signed difference between two NTP timestamps
 *
 * @param t1 Start timestamp
 * @param t2 End timestamp
 * @return t2 minus t1 in microseconds
 */
int64_t ntp_sync_diff_us(ntp_timestamp_t t1, ntp_timestamp_t t2);

/**
 * @brief Build a RAOP timing request when the next request is due
 *
 * @param sync NTP synchronizer state
 * @param packet Destination 32-byte request buffer
 * @return 1 when a request was built, 0 when not due
 */
int ntp_sync_request(ntp_sync_t *sync, uint8_t packet[32]);

/**
 * @brief Consume a RAOP timing reply and update clock offset
 *
 * @param sync NTP synchronizer state
 * @param data Timing reply bytes
 * @param len Number of bytes in the reply
 * @return 0 on success, -1 when the reply is invalid
 */
int ntp_sync_process_packet(ntp_sync_t *sync, const uint8_t *data, size_t len);

/**
 * @brief Build a RAOP timing reply for a peer request
 *
 * @param request Timing request bytes
 * @param len Number of bytes in the request
 * @param reply Destination 32-byte reply buffer
 * @return 0 on success, -1 when the request is invalid
 */
int ntp_sync_reply(const uint8_t *request, size_t len, uint8_t reply[32]);

/**
 * @brief Consume an RAOP RTP/NTP synchronization control packet
 *
 * @param sync NTP synchronizer state
 * @param data Control packet bytes
 * @param len Number of bytes in the control packet
 * @param rate Audio sample rate in frames per second
 * @return 0 on success, -1 when the packet is invalid
 */
int ntp_sync_control(ntp_sync_t *sync, const uint8_t *data, size_t len, uint32_t rate);

/**
 * @brief Convert an RTP timestamp into a local playback deadline
 *
 * @param sync NTP synchronizer state
 * @param timestamp RTP timestamp to schedule
 * @param rate Audio sample rate in frames per second
 * @param local_us Destination local deadline in microseconds
 * @return 0 when timing exchange and anchor data are available, otherwise -1
 */
int ntp_sync_deadline(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate, uint64_t *local_us);

/**
 * @brief Convert an RTP timestamp to the sender's NTP timeline
 *
 * @param sync NTP synchronizer state
 * @param timestamp RTP timestamp
 * @param rate Audio sample rate in frames per second
 * @return Corresponding sender NTP timestamp, or local time when no anchor is available
 */
ntp_timestamp_t ntp_sync_rtp_to_ntp(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate);

/**
 * @brief Get the sender-clock-minus-local-clock offset
 *
 * @param sync NTP synchronizer state
 * @return Clock offset in microseconds, or 0 when sync is NULL
 */
int64_t ntp_sync_get_offset_us(ntp_sync_t *sync);

/**
 * @brief Clear all NTP clock samples and anchors
 *
 * @param sync NTP synchronizer state
 */
void ntp_sync_deinit(ntp_sync_t *sync);
#endif
