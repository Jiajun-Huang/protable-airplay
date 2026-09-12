#include "protocol/rtp.h"
#include "protocol/sdp.h"
#include "sync/ntp_sync.h"
#include "util/log.h"
#include "util/network_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR("test", "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    uint32_t address;
    char text[16];
    CHECK(net_str_to_ipv4("192.168.1.42", &address) == 0);
    CHECK(memcmp(&address, (uint8_t[]){192, 168, 1, 42}, 4) == 0);
    CHECK(net_ipv4_to_str(address, text, sizeof(text)) == 0);
    CHECK(strcmp(text, "192.168.1.42") == 0);
    CHECK(net_str_to_ipv4("256.1.1.1", &address) < 0);
    CHECK(net_str_to_ipv4("1.2.3.4x", &address) < 0);
    CHECK(net_str_to_ipv4("1.2.3", &address) < 0);
    CHECK(net_ascii_casecmp("Content-Type", "content-type") == 0);

    uint8_t packet[] = {0x80, 0x60, 0x12, 0x34, 0, 0, 1, 0, 0, 0, 0, 9, 0x01, 0x02};
    rtp_packet_t parsed;
    CHECK(rtp_parse_packet(packet, sizeof(packet), &parsed) == 0);
    CHECK(parsed.header.sequence == 0x1234 && parsed.header.timestamp == 256);
    CHECK(parsed.payload_len == 2 && parsed.payload[1] == 2);
    uint8_t resent[sizeof(packet) + 4] = {0x80, 0xd6, 0, 1};
    memcpy(resent + 4, packet, sizeof(packet));
    CHECK(rtp_parse_packet(resent, sizeof(resent), &parsed) == 0);
    CHECK(parsed.header.payload_type == 96 && parsed.header.sequence == 0x1234 &&
          parsed.payload_len == 2);
    CHECK(rtp_parse_packet(packet, 11, &parsed) < 0);
    packet[0] = 0x90; /* Extension bit without an extension header. */
    CHECK(rtp_parse_packet(packet, sizeof(packet), &parsed) < 0);
    packet[0] = 0xa0;
    packet[13] = 3;
    CHECK(rtp_parse_packet(packet, sizeof(packet), &parsed) < 0);
    packet[13] = 1;
    CHECK(rtp_parse_packet(packet, sizeof(packet), &parsed) == 0 && parsed.payload_len == 1);

    const char sdp[] = "v=0\r\nm=audio 0 RTP/AVP 96\r\na=rtpmap:96 L16/44100/2\r\n";
    char scratch[SDP_SCRATCH_MAX];
    sdp_session_t session;
    CHECK(sdp_parse((const uint8_t *)sdp, strlen(sdp), &session, scratch, sizeof(scratch)) == 0);
    CHECK(session.codec == SDP_CODEC_PCM && session.channels == 2 && session.sample_rate == 44100);
    const char aac_sdp[] = "v=0\r\nm=audio 0 RTP/AVP 96\r\n"
                           "a=rtpmap:96 MPEG4-GENERIC/44100/2\r\n"
                           "a=fmtp:96 streamtype=5;profile-level-id=15;mode=AAC-hbr;"
                           "sizelength=13;indexlength=3;indexdeltalength=3;config=1210\r\n";
    CHECK(sdp_parse(
              (const uint8_t *)aac_sdp, strlen(aac_sdp), &session, scratch, sizeof(scratch)) == 0);
    CHECK(session.codec == SDP_CODEC_AAC && session.channels == 2 && session.sample_rate == 44100 &&
          session.frames_per_packet == 1024);
    CHECK(session.aac_config_len == 2 && session.aac_config[0] == 0x12 &&
          session.aac_config[1] == 0x10 && session.aac_size_length == 13 &&
          session.aac_index_length == 3 && session.aac_index_delta_length == 3);
    CHECK(sdp_parse((const uint8_t *)sdp, strlen(sdp), &session, scratch, 2) < 0);

    ntp_timestamp_t a = {100, 0x80000000u}, b = {101, 0};
    CHECK(ntp_sync_diff_us(a, b) == 500000);
    CHECK(ntp_sync_diff_us(b, a) == -500000);
    LOG_INFO("test", "Core protocol checks passed\n");
    return 0;
}
