#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief NTP-based timing synchronization for AirPlay audio
 * Handles timing packets to synchronize audio playback with sender
 */

typedef struct ntp_sync ntp_sync_t;

/**
 * @brief NTP timestamp (64-bit format: 32-bit seconds + 32-bit fraction)
 */
typedef struct
{
    uint32_t seconds;
    uint32_t fraction;
} ntp_timestamp_t;

/**
 * @brief Create NTP sync manager
 * @return sync instance, or NULL on error
 */
ntp_sync_t *ntp_sync_create(void);

/**
 * @brief Process NTP timing packet
 * Updates clock offset and RTT estimates
 *
 * @param sync NTP sync instance
 * @param data timing packet data
 * @param len packet length
 * @return 0 on success, negative on error
 */
int ntp_sync_process_packet(ntp_sync_t *sync, const uint8_t *data, size_t len);

/**
 * @brief Convert RTP timestamp to NTP time
 * @param sync NTP sync instance
 * @param rtp_timestamp RTP timestamp from packet
 * @param sample_rate audio sample rate (Hz)
 * @return NTP timestamp
 */
ntp_timestamp_t ntp_sync_rtp_to_ntp(ntp_sync_t *sync, uint32_t rtp_timestamp, uint32_t sample_rate);

/**
 * @brief Get current NTP time
 * @return current NTP timestamp
 */
ntp_timestamp_t ntp_sync_now(void);

/**
 * @brief Calculate time difference in microseconds
 * @param t1 first timestamp
 * @param t2 second timestamp
 * @return microseconds (t2 - t1)
 */
int64_t ntp_sync_diff_us(ntp_timestamp_t t1, ntp_timestamp_t t2);

/**
 * @brief Get clock offset between sender and receiver
 * @param sync NTP sync instance
 * @return offset in microseconds (positive = receiver ahead)
 */
int64_t ntp_sync_get_offset_us(ntp_sync_t *sync);

/**
 * @brief Close NTP sync
 * @param sync NTP sync instance
 */
void ntp_sync_close(ntp_sync_t *sync);

#endif // NTP_SYNC_H
