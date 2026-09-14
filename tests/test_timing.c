#include "audio_pipeline.h"
#include "os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static uint64_t now = UINT64_C(1700000000000000);
uint64_t os_time_us(void) { return now; }
static void put32(uint8_t *p, uint32_t n)
{ p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16); p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n; }
static void put_time(uint8_t *p, uint64_t local_us, int seconds_offset)
{
    put32(p, (uint32_t)(local_us / 1000000 + 2208988800ULL + seconds_offset));
    put32(p + 4, (uint32_t)((local_us % 1000000) * UINT64_C(4294967296) / 1000000));
}
static void exchange(ntp_sync_t *sync, int offset_seconds)
{
    uint8_t request[32], reply[32] = {0x80, 0xd3, 0, 7};
    CHECK(ntp_sync_request(sync, request) == 1);
    memcpy(reply + 8, request + 24, 8);
    put_time(reply + 16, now + 1000, offset_seconds);
    put_time(reply + 24, now + 1000, offset_seconds);
    now += 2000;
    CHECK(ntp_sync_process_packet(sync, reply, sizeof(reply)) == 0);
    CHECK(llabs(sync->clock_offset_us - (int64_t)offset_seconds * 1000000) <= 2);
    CHECK(sync->rtt_us == 2000 && sync->synchronized);
    CHECK(ntp_sync_process_packet(sync, reply, sizeof(reply)) < 0);
}
static void anchor(ntp_sync_t *sync, uint32_t rtp, uint64_t local, int offset)
{
    uint8_t packet[20] = {0x80, 0xd4, 0, 4};
    put32(packet + 4, rtp);
    put_time(packet + 8, local, offset);
    put32(packet + 16, rtp + 88200);
    CHECK(ntp_sync_control(sync, packet, sizeof(packet), 44100) == 0);
}
static void test_clock(void)
{
    ntp_sync_t sync;
    ntp_sync_init(&sync);
    exchange(&sync, 5);
    uint64_t deadline;
    uint32_t base = UINT32_MAX - 44100;
    anchor(&sync, base, now, 5);
    CHECK(sync.latency_frames == 88200);
    CHECK(ntp_sync_deadline(&sync, base + 88200, 44100, &deadline) == 0);
    CHECK(llabs((int64_t)(deadline - now) - 2000000) <= 3);
    CHECK(ntp_sync_deadline(&sync, base - 55125, 44100, &deadline) == 0);
    CHECK(llabs((int64_t)(deadline - now) + 1250000) <= 3);
    ntp_sync_init(&sync);
    exchange(&sync, -5);
    anchor(&sync, 1234, now, -5);
    CHECK(ntp_sync_deadline(&sync, 1234 + 55125, 44100, &deadline) == 0);
    CHECK(llabs((int64_t)(deadline - now) - 1250000) <= 3);
    CHECK(ntp_sync_diff_us((ntp_timestamp_t){UINT32_MAX, 0}, (ntp_timestamp_t){0, 0}) == 1000000);
    uint8_t request[32] = {0x80, 0xd2, 0x12, 0x34}, reply[32];
    put_time(request + 24, now, 0);
    CHECK(ntp_sync_reply(request, sizeof(request), reply) == 0);
    CHECK(reply[1] == 0xd3 && reply[2] == 0x12 && reply[3] == 0x34);
    CHECK(memcmp(reply + 8, request + 24, 8) == 0);
    CHECK(ntp_sync_reply(request, 31, reply) < 0);
}
static playout_t queue;
static void test_queue(void)
{
    uint8_t bytes[8] = {0};
    rtp_packet_t packet = {.header = {.sequence = 65535, .timestamp = UINT32_MAX - 351},
                           .payload = bytes, .payload_len = sizeof(bytes)};
    playout_reset(&queue);
    CHECK(playout_push(&queue, &packet) == 1);
    CHECK(playout_push(&queue, &packet) == 0);
    packet.header.sequence = 0; packet.header.timestamp += 352;
    CHECK(playout_push(&queue, &packet) == 1);
    packet.header.sequence = 65534; packet.header.timestamp -= 704;
    CHECK(playout_push(&queue, &packet) == 1);
    CHECK(playout_peek(&queue)->header.sequence == 65534);
    playout_pop(&queue, playout_peek(&queue));
    CHECK(playout_push(&queue, &packet) == 0);
    CHECK(playout_peek(&queue)->header.sequence == 65535);
    playout_pop(&queue, playout_peek(&queue));
    CHECK(playout_peek(&queue)->header.sequence == 0);
    playout_set_floor(&queue, 0, 1);
    packet.header.timestamp = UINT32_MAX;
    CHECK(playout_push(&queue, &packet) == 0);
    packet.header.timestamp = 0;
    CHECK(playout_push(&queue, &packet) == 0);
    packet.header.timestamp = 352;
    CHECK(playout_push(&queue, &packet) == 1);
    playout_reset(&queue);
    for (unsigned i = 0; i < AIRPLAY_PLAYOUT_PACKETS; ++i) {
        packet.header.sequence = (uint16_t)i;
        CHECK(playout_push(&queue, &packet) == 1);
    }
    packet.header.sequence = AIRPLAY_PLAYOUT_PACKETS;
    CHECK(playout_push(&queue, &packet) == -1);
}

/* Deterministic transport and output: exercise the real receive callback and polling scheduler. */
int net_udp_bind(net_socket_t *s, uint16_t port) { s->port = port; s->handle = port; return 0; }
void net_close(net_socket_t *s) { s->handle = UINTPTR_MAX; }
int net_wait(const net_socket_t *s, size_t n, uint8_t *ready, int timeout)
{ (void)s; (void)timeout; memset(ready, 0, n); return 0; }
int net_udp_recv(net_socket_t *s, void *b, size_t n, net_addr_t *p, int t)
{ (void)s; (void)b; (void)n; (void)p; (void)t; return NET_TIMEOUT; }
int net_udp_send(net_socket_t *s, const void *b, size_t n, const net_addr_t *p)
{ (void)s; (void)b; (void)p; return (int)n; }
static audio_pipeline_t pipeline;
static unsigned writes;
static int output_delay;
static void output(const int16_t *samples, size_t count, void *arg)
{ (void)arg; CHECK(count == 4 && samples[0] == 1000 && samples[1] == -1000); ++writes; }
static int delay(void *arg) { (void)arg; return output_delay; }
static void test_scheduled_output(void)
{
    audio_pipeline_config_t config = {.audio_port = 6000, .control_port = 6001, .timing_port = 6002,
        .on_audio_data = output, .output_delay_frames = delay};
    sdp_session_t session = {.codec = SDP_CODEC_PCM, .sample_rate = 44100, .channels = 2,
        .bits_per_sample = 16, .frames_per_packet = 2, .payload_type = 96};
    CHECK(audio_pipeline_create(&pipeline, &config) == 0);
    CHECK(audio_pipeline_configure(&pipeline, &session) == 0);
    CHECK(audio_pipeline_start(&pipeline) == 0);
    exchange(&pipeline.ntp_sync, 5);
    uint64_t start = now;
    anchor(&pipeline.ntp_sync, 1000, now, 5);
    uint8_t pcm[] = {0x03,0xe8,0xfc,0x18,0,0,0,0};
    rtp_packet_t packet = {.header = {.sequence = 10, .timestamp = 89200, .payload_type = 96},
        .payload = pcm, .payload_len = sizeof(pcm)};
    pipeline.rtp.config.audio_cb(&packet, &pipeline);
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 0);
    now = start + 1900000;
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 0);
    output_delay = 441; /* 10 ms already queued in the device. */
    now = start + 1980000;
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 0);
    now = start + 1990000;
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 1);
    pipeline.rtp.config.audio_cb(&packet, &pipeline); /* duplicate after playback */
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 1);
    ++packet.header.sequence; packet.header.timestamp += 352;
    pipeline.rtp.config.audio_cb(&packet, &pipeline);
    now = start + 2500000;
    CHECK(audio_pipeline_poll(&pipeline, 0) == 0 && writes == 1 && pipeline.late_packets == 1);
    audio_pipeline_set_start(&pipeline, 90000, 1);
    pipeline.rtp.config.audio_cb(&packet, &pipeline);
    CHECK(pipeline.playout.count == 0);
    audio_pipeline_close(&pipeline);
}
int main(void)
{
    test_clock(); test_queue(); test_scheduled_output();
    puts("Sender timing, RTP ordering, FLUSH and scheduled output passed");
    return 0;
}
