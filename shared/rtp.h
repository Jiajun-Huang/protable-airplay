#ifndef RTP_H
#define RTP_H

#include <stdint.h>
#include <stddef.h>
#include "udp_if.h"

/* Keep buffers reasonably sized for typical AirPlay RTP payloads. */
#define RTP_BUFFER_SIZE 4096

typedef struct
{
    uint8_t version;
    uint8_t padding;
    uint8_t extension;
    uint8_t csrc_count;
    uint8_t marker;
    uint8_t payload_type;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;

typedef struct
{
    rtp_header_t header;
    const uint8_t *payload;
    size_t payload_len;
} rtp_packet_t;

typedef void (*rtp_audio_callback)(const rtp_packet_t *packet, void *user_data);
typedef void (*rtp_control_callback)(const uint8_t *data, size_t len, void *user_data);
typedef void (*rtp_timing_callback)(const uint8_t *data, size_t len, void *user_data);

typedef struct
{
    uint16_t audio_port;
    uint16_t control_port;
    uint16_t timing_port;
    rtp_audio_callback audio_cb;
    rtp_control_callback control_cb;
    rtp_timing_callback timing_cb;
    void *user_data;
} rtp_receiver_config_t;

typedef struct
{
    udp_socket_t audio_socket;
    udp_socket_t control_socket;
    udp_socket_t timing_socket;
    int audio_open;
    int control_open;
    int timing_open;
    rtp_receiver_config_t config;
    uint8_t audio_buffer[RTP_BUFFER_SIZE];
    uint8_t control_buffer[RTP_BUFFER_SIZE];
    uint8_t timing_buffer[RTP_BUFFER_SIZE];
    uint32_t audio_packet_count;
} rtp_receiver_t;

int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet);
int rtp_receiver_create(rtp_receiver_t *receiver, const rtp_receiver_config_t *config);
int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms);
void rtp_receiver_close(rtp_receiver_t *receiver);

#endif
