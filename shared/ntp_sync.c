#include "ntp_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/**
 * @brief NTP synchronization implementation
 * Uses timing packets to estimate clock offset and RTT
 */

#define NTP_EPOCH_OFFSET 2208988800ULL // Seconds between 1900 (NTP) and 1970 (Unix)

typedef struct ntp_sync
{
    int64_t clock_offset_us;  // Estimated offset (receiver - sender)
    int64_t rtt_us;           // Round-trip time
    uint32_t rtp_base;        // Base RTP timestamp
    ntp_timestamp_t ntp_base; // Corresponding NTP time
    int synchronized;
} ntp_sync_internal_t;

ntp_sync_t *ntp_sync_create(void)
{
    ntp_sync_internal_t *sync = (ntp_sync_internal_t *)calloc(1, sizeof(*sync));
    if (!sync)
        return NULL;

    printf("[ntp] Created timing sync\n");
    return (ntp_sync_t *)sync;
}

ntp_timestamp_t ntp_sync_now(void)
{
    ntp_timestamp_t ts = {0};

#ifdef _WIN32
    // Windows: Use GetSystemTimeAsFileTime
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    // Convert FILETIME (100ns since 1601) to Unix time
    uint64_t windows_time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    uint64_t unix_time = (windows_time / 10000000ULL) - 11644473600ULL;

    // Convert Unix to NTP
    uint64_t ntp_time = unix_time + NTP_EPOCH_OFFSET;
    ts.seconds = (uint32_t)(ntp_time);

    // Fractional part
    uint64_t fraction = (windows_time % 10000000ULL) * 429496729ULL / 1000000ULL;
    ts.fraction = (uint32_t)fraction;
#else
    // POSIX: Use gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t ntp_time = (uint64_t)tv.tv_sec + NTP_EPOCH_OFFSET;
    ts.seconds = (uint32_t)ntp_time;
    ts.fraction = (uint32_t)((uint64_t)tv.tv_usec * 4294967296ULL / 1000000ULL);
#endif

    return ts;
}

int64_t ntp_sync_diff_us(ntp_timestamp_t t1, ntp_timestamp_t t2)
{
    // Calculate difference in microseconds
    int64_t sec_diff = (int64_t)t2.seconds - (int64_t)t1.seconds;
    int64_t frac_diff = (int64_t)t2.fraction - (int64_t)t1.fraction;

    // Convert fraction (2^32 per second) to microseconds
    int64_t frac_us = (frac_diff * 1000000LL) / 4294967296LL;

    return (sec_diff * 1000000LL) + frac_us;
}

int ntp_sync_process_packet(ntp_sync_t *sync, const uint8_t *data, size_t len)
{
    if (!sync || !data || len < 32)
        return -1;

    ntp_sync_internal_t *impl = (ntp_sync_internal_t *)sync;

    // Parse NTP timing packet (simplified)
    // Format: origin_timestamp (8), receive_timestamp (8), transmit_timestamp (8)

    ntp_timestamp_t origin = {
        .seconds = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3],
        .fraction = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]};

    ntp_timestamp_t receive = {
        .seconds = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11],
        .fraction = (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15]};

    ntp_timestamp_t transmit = {
        .seconds = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19],
        .fraction = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23]};

    ntp_timestamp_t destination = ntp_sync_now();

    // Calculate offset and RTT using NTP algorithm
    // offset = ((receive - origin) + (transmit - destination)) / 2
    // rtt = (destination - origin) - (transmit - receive)

    int64_t t1 = ntp_sync_diff_us(origin, receive);       // Time on sender
    int64_t t2 = ntp_sync_diff_us(transmit, destination); // Time on receiver
    int64_t rtt = ntp_sync_diff_us(origin, destination) - ntp_sync_diff_us(receive, transmit);

    impl->clock_offset_us = (t1 + t2) / 2;
    impl->rtt_us = rtt;
    impl->synchronized = 1;

    // Debug output (every 10th packet)
    static int count = 0;
    if (++count % 10 == 0)
    {
        printf("[ntp] Offset: %lld us, RTT: %lld us\n",
               (long long)impl->clock_offset_us, (long long)impl->rtt_us);
    }

    return 0;
}

ntp_timestamp_t ntp_sync_rtp_to_ntp(ntp_sync_t *sync, uint32_t rtp_timestamp, uint32_t sample_rate)
{
    if (!sync || sample_rate == 0)
    {
        return ntp_sync_now();
    }

    ntp_sync_internal_t *impl = (ntp_sync_internal_t *)sync;

    if (!impl->synchronized)
    {
        // No sync yet, use current time
        impl->rtp_base = rtp_timestamp;
        impl->ntp_base = ntp_sync_now();
    }

    // Calculate time offset from base RTP timestamp
    int32_t rtp_diff = (int32_t)(rtp_timestamp - impl->rtp_base);
    int64_t time_diff_us = ((int64_t)rtp_diff * 1000000LL) / sample_rate;

    // Add to base NTP time
    ntp_timestamp_t result = impl->ntp_base;

    int64_t total_frac = result.fraction + (time_diff_us * 4294967296LL / 1000000LL);
    result.seconds += (uint32_t)(time_diff_us / 1000000LL);
    result.seconds += (uint32_t)(total_frac / 4294967296LL);
    result.fraction = (uint32_t)(total_frac % 4294967296LL);

    return result;
}

int64_t ntp_sync_get_offset_us(ntp_sync_t *sync)
{
    if (!sync)
        return 0;

    ntp_sync_internal_t *impl = (ntp_sync_internal_t *)sync;
    return impl->clock_offset_us;
}

void ntp_sync_close(ntp_sync_t *sync)
{
    if (sync)
    {
        printf("[ntp] Closed timing sync\n");
        free(sync);
    }
}
