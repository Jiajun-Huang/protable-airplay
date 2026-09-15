#include "sync/ptp_sync.h"
#include "airplay_config.h"
#include "os.h"
#include "util/log.h"
#include <string.h>

/**
 * @brief Convert big-endian bytes to a 64-bit integer
 *
 * @param p Input bytes in network byte order
 * @param n Number of bytes to convert
 * @return Converted unsigned integer
 */
static uint64_t be(const uint8_t *p, unsigned n)
{
    /* PTP fields are network-order integers and may be wider than 32 bits. */
    uint64_t value = 0;
    while (n--)
        value = (value << 8) | *p++;
    return value;
}

void ptp_sync_init(ptp_sync_t *p)
{
    memset(p, 0, sizeof(*p));
    p->sockets[0] = p->sockets[1] = (net_socket_t)NET_SOCKET_INIT;
}

void ptp_sync_close(ptp_sync_t *p)
{
    net_close(&p->sockets[0]);
    net_close(&p->sockets[1]);
    ptp_sync_init(p);
}

int ptp_sync_open(ptp_sync_t *p, const char *ip) // Open PTP synchronization
{
    if (p->opened)
        return 0;
    // Bind to the PTP event and general multicast ports, and join the multicast group on the specified local IPv4 interface.
    if (net_udp_bind(&p->sockets[0], AIRPLAY_PTP_EVENT_PORT) ||
        net_udp_bind(&p->sockets[1], AIRPLAY_PTP_GENERAL_PORT) ||
        net_udp_join(&p->sockets[0], "224.0.1.129", ip) ||
        net_udp_join(&p->sockets[1], "224.0.1.129", ip))
    {
        ptp_sync_close(p); 
        return -1;
    }
    p->opened = 1;
    return 0;
}

void ptp_sync_set_clock(ptp_sync_t *p, uint64_t clock_id)
{
    if (p->clock_id != clock_id)
    {
        /* Samples from a previous sender clock cannot be reused for this clock. */
        p->clock_id = clock_id;
        p->pending = p->ready = 0;
    }
}

int ptp_sync_packet(ptp_sync_t *p, const uint8_t *data, size_t size, uint64_t now)
{
    if (!p || !data || size < 44 || (data[1] & 15) != 2)
        return -1;
    size_t length = (size_t)be(data + 2, 2);
    if (length < 44 || length > size)
        return -1;
    unsigned type = data[0] & 15;
    uint64_t clock = be(data + 20, 8);
    if ((type != 0 && type != 8) || !p->clock_id || clock != p->clock_id) // 
        return 0;
    uint16_t seq = (uint16_t)be(data + 30, 2);
    /* A two-step Sync is completed by the matching Follow_Up message. */
    int64_t correction = (int64_t)be(data + 8, 8) / INT64_C(65536000);
    if (type == 0)
    {
        p->sequence = seq;
        p->domain = data[4];
        p->sync_received_us = now;
        memcpy(p->source_port, data + 20, 10);
        p->sync_correction = correction;
        p->pending = 1;
        if (data[6] & 2)
            return 0;
    }
    else
    {
        if (!p->pending || seq != p->sequence || data[4] != p->domain ||
            memcmp(p->source_port, data + 20, 10) || now - p->sync_received_us > 1000000)
            return 0;
        correction += p->sync_correction;
    }
    uint64_t sec = be(data + 34, 6); 
    uint32_t ns = be(data + 40, 4);
    if (ns >= 1000000000 || sec > INT64_MAX / 1000000)
        return -1;
    /* Convert the sender timestamp to microseconds and compare it with local receipt time. */
    int64_t offset =
        (int64_t)(sec * 1000000 + ns / 1000) + correction - (int64_t)p->sync_received_us;
    /* Receive-only PTP: prefer samples with the least one-way network delay. */
    if (!p->ready || now - p->updated_us > 2000000 || offset > p->offset_us)
        p->offset_us = offset;
    else
        p->offset_us += (offset - p->offset_us) / 64;
    if (!p->ready)
        LOG_INFO( "Sender clock ready: %016llx\n", (unsigned long long)clock);
    p->ready = 1;
    p->pending = 0;
    p->updated_us = now;
    return 1;
}

int ptp_sync_poll(ptp_sync_t *p)
{
    if (!p->opened)
        return 0;
    uint8_t data[512];
    net_addr_t peer;
    /* Drain both sockets without blocking; bounded batches keep audio polling responsive. */
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned batch = 0; batch < 8; ++batch)
        {
            int n = net_udp_recv(&p->sockets[i], data, sizeof(data), &peer, 0);
            if (n == NET_TIMEOUT)
                break;
            if (n == NET_ERROR)
                return -1;
            ptp_sync_packet(p, data, (size_t)n, os_time_us());
        }
    return 0;
}

int ptp_sync_anchor(airplay_anchor_t *anchor, const uint8_t *data, size_t size)
{
    if (!anchor || !data || size < 28 || (data[0] >> 6) != 2 || (data[1] & 0x7f) != 0x57 ||
        !be(data + 20, 8))
        return -1;
    /* AirPlay's control anchor binds one RTP timestamp to sender PTP time. */
    *anchor = (airplay_anchor_t){.rtp_time = (uint32_t)be(data + 4, 4),
                                 .network_us = be(data + 8, 8) / 1000,
                                 .clock_id = be(data + 20, 8),
                                 .valid = 1,
                                 .playing = 1};
    return 0;
}

int ptp_sync_deadline(const ptp_sync_t *p,
                      const airplay_anchor_t *a,
                      uint32_t timestamp,
                      uint32_t rate,
                      uint32_t latency,
                      uint64_t *deadline)
{
    uint64_t now = os_time_us();
    if (!p->ready || !a->valid || !a->playing || !rate || a->clock_id != p->clock_id ||
        (now >= p->updated_us && now - p->updated_us > 5000000))
        return -1;
    /* Add playout latency before converting RTP frames to microseconds. */
    int64_t delta = (int64_t)(int32_t)(timestamp - a->rtp_time) + latency;
    int64_t local = (int64_t)a->network_us - p->offset_us + delta * 1000000 / rate;
    if (local < 0)
        return -1;
    *deadline = (uint64_t)local;
    return 0;
}
