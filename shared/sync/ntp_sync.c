#include "sync/ntp_sync.h"
#include "os.h"
#include "util/log.h"
#include <string.h>

#define NTP_EPOCH_OFFSET              2208988800ULL
#define NTP_PACKET_SIZE               32
#define NTP_VERSION                   2
#define NTP_VERSION_HEADER            0x80
#define NTP_REQUEST_HEADER            0xd2
#define NTP_REPLY_HEADER              0xd3
#define NTP_MESSAGE_TYPE_MASK         0x7f
#define NTP_REQUEST_TYPE              0x52
#define NTP_REPLY_TYPE                0x53
#define NTP_REQUEST_SEQUENCE          7
#define NTP_ORIGIN_TIMESTAMP_OFFSET   8
#define NTP_RECEIVE_TIMESTAMP_OFFSET  16
#define NTP_TRANSMIT_TIMESTAMP_OFFSET 24

/*
NTP format (32 bytes):
+--------+--------+--------+--------+
| LI() | VN | Mode |    Reserved    |
+--------+--------+--------+--------+
|          Originate Timestamp (32 bits)         |
+-----------------------------------------------+
|          Receive Timestamp (32 bits)             |
+-----------------------------------------------+
|          Transmit Timestamp (32 bits)            |
+-----------------------------------------------+
*/

/**
 * @brief read32.
 * @param p Parameter named p.
 * @return Function result.
 */
static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
/**
 * @brief write32.
 * @param p Parameter named p.
 * @param v Parameter named v.
 */
static void write32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
/**
 * @brief read_time.
 * @param p Parameter named p.
 * @return Function result.
 */
static ntp_timestamp_t read_time(const uint8_t *p)
{
    return (ntp_timestamp_t){read32(p), read32(p + 4)};
}
/**
 * @brief write_time.
 * @param p Parameter named p.
 * @param t Parameter named t.
 */
static void write_time(uint8_t *p, ntp_timestamp_t t)
{
    write32(p, t.seconds);
    write32(p + 4, t.fraction);
}
/**
 * @brief Convert microseconds to NTP timestamp.
 * @param us Parameter named us.
 * @return Function result.
 */
static ntp_timestamp_t from_us(uint64_t us)
{
    /* Convert the monotonic microsecond clock used by the platform layer into NTP time. */
    return (ntp_timestamp_t){(uint32_t)(us / 1000000 + NTP_EPOCH_OFFSET),
                             (uint32_t)((us % 1000000) * UINT64_C(4294967296) / 1000000)};
}
int ntp_sync_init(ntp_sync_t *sync)
{
    if (!sync)
        return -1;
    memset(sync, 0, sizeof(*sync));
    return 0;
}
ntp_timestamp_t ntp_sync_now(void)
{
    return from_us(os_time_us());
}
int64_t ntp_sync_diff_us(ntp_timestamp_t a, ntp_timestamp_t b)
{
    /* Signed modular seconds also handles the NTP era rollover. */
    return (int64_t)(int32_t)(b.seconds - a.seconds) * 1000000 +
           ((int64_t)b.fraction - a.fraction) * 1000000 / INT64_C(4294967296);
}
int ntp_sync_request(ntp_sync_t *sync, uint8_t packet[NTP_PACKET_SIZE])
{
    uint64_t now = os_time_us();
    if (now < sync->next_request_us)
        return 0;
    memset(packet, 0, NTP_PACKET_SIZE);
    packet[0] = NTP_VERSION_HEADER;
    packet[1] = NTP_REQUEST_HEADER;
    packet[3] = NTP_REQUEST_SEQUENCE;
    /* The originate timestamp in the reply must match this exact transmit time. */
    sync->request_local_us = now;      // t1
    sync->request_time = from_us(now); // t1 in NTP format
    write_time(&packet[NTP_TRANSMIT_TIMESTAMP_OFFSET], sync->request_time);
    sync->request_pending = 1;

    // next due time 250 ms for first 4 requests, then 2 s afterwards
    sync->next_request_us = now + (++sync->requests <= 4 ? 250000 : 2000000);
    return 1;
}
int ntp_sync_process_packet(ntp_sync_t *sync, const uint8_t *data, size_t len)
{
    if (!sync || !data || len < NTP_PACKET_SIZE || (data[0] >> 6) != NTP_VERSION ||
        (data[1] & NTP_MESSAGE_TYPE_MASK) != NTP_REPLY_TYPE || !sync->request_pending)
    {
        LOG_WARN("Invalid NTP reply: version=%u type=%u pending=%d\n",
                 (unsigned)(data[0] >> 6),
                 (unsigned)(data[1] & NTP_MESSAGE_TYPE_MASK),
                 sync->request_pending);
        return -1;
    }
    uint64_t now = os_time_us();                                            // t1
    ntp_timestamp_t origin = read_time(&data[NTP_ORIGIN_TIMESTAMP_OFFSET]); // t1 in NTP format
    if (origin.seconds != sync->request_time.seconds ||
        origin.fraction != sync->request_time.fraction)
    {
        LOG_WARN("NTP reply does not match request: origin=%u.%u request=%u.%u\n",
                 origin.seconds,
                 origin.fraction,
                 sync->request_time.seconds,
                 sync->request_time.fraction);
        return -1;
    }
    ntp_timestamp_t receive = read_time(&data[NTP_RECEIVE_TIMESTAMP_OFFSET]), // t2 in NTP format
        transmit = read_time(&data[NTP_TRANSMIT_TIMESTAMP_OFFSET]);           // t3 in NTP format
    /* NTP timing exchange: t1=request sent, t2=peer received, t3=peer sent, t4=reply received. */
    int64_t processing = ntp_sync_diff_us(receive, transmit);
    int64_t rtt = (int64_t)(now - sync->request_local_us) - processing;
    if (processing < 0 || rtt < -2 || rtt > 500000)
        return -1;
    if (rtt < 0)
        rtt = 0;
    /* Estimate peer clock minus local clock while cancelling symmetric network delay. */
    int64_t offset =
        (ntp_sync_diff_us(origin, receive) + ntp_sync_diff_us(from_us(now), transmit)) / 2;
    sync->request_pending = 0;
    ++sync->samples;
    /* Prefer low-delay exchanges. Refresh the reference to follow slow clock drift. */
    if (!sync->synchronized || rtt <= sync->rtt_us + 500 || now - sync->best_sample_us > 10000000)
    {
        sync->clock_offset_us = offset;
        sync->rtt_us = rtt;
        sync->best_sample_us = now;
        sync->synchronized = 1;
    }
    return 0;
}
int ntp_sync_reply(const uint8_t *request, size_t len, uint8_t reply[32])
{
    if (!request || len < NTP_PACKET_SIZE || (request[0] >> 6) != NTP_VERSION ||
        (request[1] & NTP_MESSAGE_TYPE_MASK) != NTP_REQUEST_TYPE)
        return -1;
    ntp_timestamp_t now = ntp_sync_now();
    memset(reply, 0, NTP_PACKET_SIZE);
    reply[0] = NTP_VERSION_HEADER;
    reply[1] = NTP_REPLY_HEADER;
    reply[2] = request[2];
    reply[3] = request[3];
    /* Copy the origin timestamp from the request to the reply. */
    memcpy(&reply[NTP_ORIGIN_TIMESTAMP_OFFSET],
           &request[NTP_ORIGIN_TIMESTAMP_OFFSET],
           sizeof(ntp_timestamp_t));
    write_time(&reply[NTP_RECEIVE_TIMESTAMP_OFFSET], now);
    write_time(&reply[NTP_TRANSMIT_TIMESTAMP_OFFSET], ntp_sync_now());
    return 0;
}
int ntp_sync_control(ntp_sync_t *sync, const uint8_t *data, size_t len, uint32_t rate)
{
    if (!sync || !data || len < 20 || (data[0] >> 6) != 2 || (data[1] & 0x7f) != 0x54 || !rate)
        return -1;
    /* This control packet binds an RTP timestamp to the sender's NTP timeline. */
    uint32_t playing = read32(&data[4]);
    uint32_t sending = read32(&data[16]);
    uint32_t latency = sending - playing;
    if (latency > (uint64_t)rate * 10) //
        return -1;
    sync->rtp_base = playing;
    sync->ntp_base = read_time(&data[8]);
    sync->latency_frames = latency;
    sync->anchor_valid = 1;
    return 0;
}
ntp_timestamp_t ntp_sync_rtp_to_ntp(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate)
{
    if (!sync || !sync->anchor_valid || !rate)
        return ntp_sync_now();
    /* RTP advances at sample-rate ticks; signed subtraction preserves timestamp wraparound. */
    int64_t delta = (int64_t)(int32_t)(timestamp - sync->rtp_base) * 1000000 / rate;
    int64_t seconds = delta / 1000000;
    int64_t fraction =
        (int64_t)sync->ntp_base.fraction + (delta % 1000000) * INT64_C(4294967296) / 1000000;
    if (fraction < 0)
    {
        fraction += INT64_C(4294967296);
        --seconds;
    }
    if (fraction >= INT64_C(4294967296))
    {
        fraction -= INT64_C(4294967296);
        ++seconds;
    }
    return (ntp_timestamp_t){sync->ntp_base.seconds + (uint32_t)seconds, (uint32_t)fraction};
}
int ntp_sync_deadline(ntp_sync_t *sync, uint32_t timestamp, uint32_t rate, uint64_t *local_us)
{
    if (!sync || !sync->synchronized || !sync->anchor_valid || !rate || !local_us)
        return -1;
    uint64_t now = os_time_us();
    ntp_timestamp_t remote = ntp_sync_rtp_to_ntp(sync, timestamp, rate);
    /* Translate the sender's target time back into the local clock domain. */
    int64_t delta = ntp_sync_diff_us(from_us(now), remote) - sync->clock_offset_us;
    *local_us = (uint64_t)((int64_t)now + delta);
    return 0;
}
int64_t ntp_sync_get_offset_us(ntp_sync_t *sync)
{
    return sync ? sync->clock_offset_us : 0;
}
void ntp_sync_deinit(ntp_sync_t *sync)
{
    if (sync)
        memset(sync, 0, sizeof(*sync));
}
