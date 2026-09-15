





#ifndef RTP_H
#define RTP_H

#include "net.h"
#include <stddef.h>
#include <stdint.h>

#include "airplay_config.h"

/* RTP/RTCP receive module for AirPlay audio, control, retransmission, and timing sockets. */

/* Parsed fixed RTP header fields used by the audio pipeline. */
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

/* Borrowed packet view; payload points into the receive buffer. */
typedef struct
{
    rtp_header_t header;
    const uint8_t *payload;
    size_t payload_len;
} rtp_packet_t;

/* Callback invoked synchronously for one parsed audio packet. */
/**
 * @brief void.
 * @param packet Parameter named packet.
 * @param user_data Parameter named user_data.
 * @return Function result.
 */
typedef void (*rtp_audio_callback)(const rtp_packet_t *packet, void *user_data);
/* Callback invoked synchronously for an RTCP control datagram. */
/**
 * @brief void.
 * @param data Parameter named data.
 * @param len Parameter named len.
 * @param user_data Parameter named user_data.
 * @return Function result.
 */
typedef void (*rtp_control_callback)(const uint8_t *data, size_t len, void *user_data);
/* Callback invoked synchronously for a timing datagram and its source. */
/**
 * @brief void.
 * @param data Parameter named data.
 * @param len Parameter named len.
 * @param peer Parameter named peer.
 * @param user_data Parameter named user_data.
 * @return Function result.
 */
typedef void (*rtp_timing_callback)(const uint8_t *data,
                                    size_t len,
                                    const net_addr_t *peer,
                                    void *user_data);

/* Ports and callbacks required to create one receiver. */
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

/* Caller-owned sockets and receive buffers for one active sender. */
typedef struct
{
    net_socket_t audio_socket;
    net_socket_t control_socket;
    net_socket_t timing_socket;
    rtp_receiver_config_t config;
    uint8_t audio_buffer[RTP_BUFFER_SIZE];
    uint8_t control_buffer[RTP_BUFFER_SIZE];
    uint8_t timing_buffer[RTP_BUFFER_SIZE];
    uint32_t audio_packet_count;
    char peer_ip[16];
} rtp_receiver_t;

/* Parse one RTP datagram into a borrowed packet view. */
/**
 * @brief rtp_parse_packet.
 * @param data Parameter named data.
 * @param len Parameter named len.
 * @param packet Parameter named packet.
 * @return Function result.
 */
int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet);
/* Bind all UDP sockets and copy the receiver configuration. */
/**
 * @brief rtp_receiver_create.
 * @param receiver Parameter named receiver.
 * @param config Parameter named config.
 * @return Function result.
 */
int rtp_receiver_create(rtp_receiver_t *receiver, const rtp_receiver_config_t *config);
/* Poll the audio, control, and timing sockets once and invoke ready callbacks. */
/**
 * @brief rtp_receiver_poll.
 * @param receiver Parameter named receiver.
 * @param timeout_ms Parameter named timeout_ms.
 * @return Function result.
 */
int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms);
/* Close all receiver sockets and clear their state. */
/**
 * @brief rtp_receiver_close.
 * @param receiver Parameter named receiver.
 */
void rtp_receiver_close(rtp_receiver_t *receiver);

#endif
