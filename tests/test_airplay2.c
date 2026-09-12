#include "airplay/pairing.h"
#include "airplay/fairplay.h"
#include "bplist.h"
#include "ptp_sync.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { LOG_ERROR("test", "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static uint64_t now = 10000000;
uint64_t os_time_us(void) { return now; }
int net_udp_bind(net_socket_t *s, uint16_t port) { s->handle = port; s->port = port; return 0; }
int net_udp_join(net_socket_t *s, const char *group, const char *ip)
{ (void)s; (void)group; (void)ip; return 0; }
void net_close(net_socket_t *s) { s->handle = UINTPTR_MAX; s->port = 0; }
int net_udp_recv(net_socket_t *s, void *data, size_t size, net_addr_t *peer, int timeout)
{ (void)s; (void)data; (void)size; (void)peer; (void)timeout; return NET_TIMEOUT; }

static size_t unhex(const char *text, uint8_t *out)
{
    size_t n = 0;
    while (text[0] && text[1]) {
        unsigned a = text[0] <= '9' ? text[0] - '0' : text[0] - 'a' + 10;
        unsigned b = text[1] <= '9' ? text[1] - '0' : text[1] - 'a' + 10;
        out[n++] = (uint8_t)(a * 16 + b); text += 2;
    }
    return n;
}
static void test_plist(void)
{
    /* Encoded independently with Python plistlib. */
    uint8_t data[256];
    size_t size = unhex(
        "62706c6973743030d20102030454726174655773747265616d73233ff0000000000000a105"
        "d2060708095373686b54747970654f1020000102030405060708090a0b0c0d0e0f10111213"
        "1415161718191a1b1c1d1e1f1067080d121a23252a2e3356000000000000010100000000"
        "0000000a00000000000000000000000000000058", data);
    bplist_t p; double rate; uint64_t type;
    CHECK(!bplist_open(&p, data, size));
    CHECK(!bplist_real(&p, bplist_get(&p, p.root, "rate"), &rate) && rate == 1.0);
    uint32_t streams = bplist_get(&p, p.root, "streams");
    CHECK(bplist_count(&p, streams) == 1 && bplist_at(&p, streams, 1) == BPLIST_NONE);
    uint32_t stream = bplist_at(&p, streams, 0);
    CHECK(!bplist_uint(&p, bplist_get(&p, stream, "type"), &type) && type == 103);
    const uint8_t *key; size_t key_size;
    CHECK(!bplist_bytes(&p, bplist_get(&p, stream, "shk"), &key, &key_size) && key_size == 32);
    for (unsigned i = 0; i < 32; ++i) CHECK(key[i] == i);
    CHECK(bplist_get(&p, stream, "absent") == BPLIST_NONE);
    for (size_t i = 0; i < size; ++i) {
        bplist_t truncated;
        if (!bplist_open(&truncated, data, i))
            (void)bplist_get(&truncated, truncated.root, "streams");
    }
    data[size - 26] = 0;
    CHECK(bplist_open(&p, data, size) < 0);
    bplist_writer_t w; uint8_t small[40];
    bplist_writer_init(&w, small, sizeof(small));
    uint32_t ref = bplist_add_string(&w, "this string cannot fit together with an offset table and trailer");
    CHECK(bplist_finish(&w, ref) == 0);
}
static void test_records(void)
{
    /* Encoded independently with Python cryptography, counter zero, key 00..1f. */
    uint8_t record[64], output[PAIR_RECORD_MAX], sealed[PAIR_RECORD_MAX + 18];
    size_t size = unhex("160057e81678e2a8f5f139410e35fc136116d681feffeca7cfb090faa6fb8d99ace065591b18a2cd", record);
    static const char text[] = "OPTIONS * RTSP/1.0\r\n\r\n";
    pairing_t p = {0}; p.established = 1;
    for (unsigned i = 0; i < 32; ++i) p.read_key[i] = p.write_key[i] = (uint8_t)i;
    CHECK(pairing_open(&p, record, size, output) == sizeof(text) - 1);
    CHECK(!memcmp(output, text, sizeof(text) - 1) && p.read_counter == 1);
    CHECK(pairing_open(&p, record, size, output) < 0 && p.read_counter == 1);
    CHECK(pairing_seal(&p, (const uint8_t *)text, sizeof(text) - 1, sealed) == (int)size);
    CHECK(!memcmp(sealed, record, size));
    p.read_counter = 0; record[size - 1] ^= 1;
    CHECK(pairing_open(&p, record, size, output) < 0 && p.read_counter == 0);
    record[1] = 0xff;
    CHECK(pairing_open(&p, record, size, output) < 0);
    p.write_counter = UINT64_MAX;
    CHECK(pairing_seal(&p, (const uint8_t *)text, sizeof(text) - 1, sealed) < 0);
    pairing_close(&p);
    CHECK(!p.established && !p.read_counter && !p.write_counter);
}
static void test_pairing_rejection(void)
{
    pairing_t p = {0}; uint8_t out[512]; size_t length;
    const uint8_t m1[] = {6,1,1, 0,1,0, 19,1,16};
    CHECK(!pairing_setup(&p, m1, sizeof(m1), out, sizeof(out), &length));
    CHECK(length > 384 && out[0] == 6 && out[2] == 2 && !p.established);
    uint8_t bad_m3[72] = {6,1,3, 3,1,0, 4,64};
    CHECK(!pairing_setup(&p, bad_m3, sizeof(bad_m3), out, sizeof(out), &length));
    CHECK(length == 6 && out[2] == 4 && out[3] == 7 && !p.established && !p.srp);
    const uint8_t truncated[] = {6, 2, 1};
    CHECK(pairing_setup(&p, truncated, sizeof(truncated), out, sizeof(out), &length) < 0);
    pairing_close(&p);
}
static void test_ptp(void)
{
    ptp_sync_t p; ptp_sync_init(&p);
    CHECK(!ptp_sync_open(&p, "127.0.0.1"));
    ptp_sync_set_clock(&p, 42);
    uint8_t sync[44] = {0x10, 2, 0, 44};
    sync[6] = 2; sync[27] = 42; sync[29] = 1; sync[31] = 7;
    CHECK(ptp_sync_packet(&p, sync, sizeof(sync), now) == 0 && !p.ready);
    uint8_t follow[44]; memcpy(follow, sync, sizeof(follow)); follow[0] = 0x18; follow[6] = 0;
    follow[39] = 2; /* Two seconds on the sender clock. */
    follow[31] = 8;
    CHECK(ptp_sync_packet(&p, follow, sizeof(follow), now + 100) == 0 && !p.ready);
    follow[31] = 7;
    CHECK(ptp_sync_packet(&p, follow, sizeof(follow), now + 100) == 1 && p.offset_us == -8000000);
    airplay_anchor_t anchor = {.clock_id = 42, .network_us = 3000000,
        .rtp_time = UINT32_MAX - 44099, .valid = 1, .playing = 1};
    uint64_t deadline;
    CHECK(!ptp_sync_deadline(&p, &anchor, 0, 44100, 0, &deadline) && deadline == 12000000);
    CHECK(!ptp_sync_deadline(&p, &anchor, 0, 44100, 11025, &deadline) && deadline == 12250000);
    anchor.playing = 0;
    CHECK(ptp_sync_deadline(&p, &anchor, 0, 44100, 0, &deadline) < 0);
    ptp_sync_set_clock(&p, 43);
    CHECK(!p.ready && !p.pending);
    CHECK(ptp_sync_packet(&p, follow, sizeof(follow), now) == 0 && !p.ready);
    ptp_sync_close(&p);
}
static void test_fairplay(void)
{
    uint8_t stage = 0, other = 0, response[FAIRPLAY_RESPONSE_MAX];
    uint8_t first[16] = {'F','P','L','Y',3,1,1,0,0,0,0,4,2,0,0,0};
    uint8_t second[164] = {'F','P','L','Y',3,1,3,0,0,0,0,152};
    for (unsigned i = 12; i < sizeof(second); ++i) second[i] = (uint8_t)i;
    CHECK(fairplay_setup(&stage, second, sizeof(second), response, sizeof(response)) < 0);
    for (unsigned mode = 0; mode < 4; ++mode) {
        first[14] = (uint8_t)mode;
        CHECK(fairplay_setup(&stage, first, sizeof(first), response, sizeof(response)) == 142);
        CHECK(stage == 1 && response[6] == 2 && response[11] == 130 && response[13] == mode);
        CHECK(fairplay_setup(&stage, second, sizeof(second), response, sizeof(response)) == 32);
        CHECK(stage == 2 && response[6] == 4 && response[11] == 20);
        CHECK(!memcmp(response + 12, second + 144, 20));
        CHECK(fairplay_setup(&stage, second, sizeof(second), response, sizeof(response)) < 0);
    }
    CHECK(fairplay_setup(&other, second, sizeof(second), response, sizeof(response)) < 0);
    first[14] = 0;
    CHECK(fairplay_setup(&stage, first, sizeof(first), response, 141) < 0 && stage == 2);
    first[14] = 4;
    CHECK(fairplay_setup(&stage, first, sizeof(first), response, sizeof(response)) < 0);
    first[14] = 0;
    for (size_t n = 0; n < sizeof(first); ++n)
        CHECK(fairplay_setup(&stage, first, n, response, sizeof(response)) < 0);
    const unsigned fields[] = {0, 4, 5, 6, 7, 8, 11};
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        first[fields[i]] ^= 0x80;
        CHECK(fairplay_setup(&stage, first, sizeof(first), response, sizeof(response)) < 0);
        first[fields[i]] ^= 0x80;
    }
    CHECK(fairplay_setup(&stage, first, sizeof(first), response, sizeof(response)) == 142);
    CHECK(fairplay_setup(&stage, second, sizeof(second), response, 31) < 0 && stage == 1);
    for (size_t n = 0; n < sizeof(second); ++n)
        CHECK(fairplay_setup(&stage, second, n, response, sizeof(response)) < 0);
}

int main(void)
{
    test_plist(); test_records(); test_pairing_rejection(); test_ptp(); test_fairplay();
    LOG_INFO("test", "AirPlay 2 plist, authenticated records, pairing rejection and PTP checks passed\n");
    return 0;
}
