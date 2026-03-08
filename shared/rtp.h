// #ifndef RTP_H
// #define RTP_H

// #include <stdint.h>
// #include <stddef.h>
// #include "udp_if.h"

// /**
//  * @brief RTP (Real-time Transport Protocol) receiver for AirPlay audio
//  * Receives and parses RTP audio packets from UDP sockets.
//  */

// #define RTP_BUFFER_SIZE 2048

// /**
//  * @brief RTP packet header structure (RFC 3550)
//  */
// typedef struct
// {
//     uint8_t version;      // RTP version (should be 2)
//     uint8_t padding;      // Padding flag
//     uint8_t extension;    // Extension flag
//     uint8_t csrc_count;   // CSRC count
//     uint8_t marker;       // Marker bit
//     uint8_t payload_type; // Payload type
//     uint16_t sequence;    // Sequence number
//     uint32_t timestamp;   // RTP timestamp
//     uint32_t ssrc;        // Synchronization source identifier
// } rtp_header_t;

// /**
//  * @brief Parsed RTP packet
//  */
// typedef struct
// {
//     rtp_header_t header;
//     const uint8_t *payload;
//     size_t payload_len;
// } rtp_packet_t;

// /**
//  * @brief Callback when RTP audio packet is received
//  * @param packet parsed RTP packet
//  * @param user_data context pointer
//  */
// typedef void (*rtp_audio_callback)(const rtp_packet_t *packet, void *user_data);

// /**
//  * @brief Callback when RTP control packet is received (RTCP)
//  * @param data raw packet data
//  * @param len packet length
//  * @param user_data context pointer
//  */
// typedef void (*rtp_control_callback)(const uint8_t *data, size_t len, void *user_data);

// /**
//  * @brief Callback when RTP timing packet is received
//  * @param data raw packet data
//  * @param len packet length
//  * @param user_data context pointer
//  */
// typedef void (*rtp_timing_callback)(const uint8_t *data, size_t len, void *user_data);

// /**
//  * @brief RTP receiver configuration
//  */
// typedef struct
// {
//     uint16_t audio_port;   // UDP port for audio data (RTP)
//     uint16_t control_port; // UDP port for control data (RTCP)
//     uint16_t timing_port;  // UDP port for timing/sync
//     rtp_audio_callback audio_cb;
//     rtp_control_callback control_cb;
//     rtp_timing_callback timing_cb;
//     void *user_data;
// } rtp_receiver_config_t;

// // Transparent structure for user-managed memory allocation
// typedef struct
// {
//     udp_socket_t audio_socket;
//     udp_socket_t control_socket;
//     udp_socket_t timing_socket;
//     rtp_receiver_config_t config;
//     uint8_t audio_buffer[RTP_BUFFER_SIZE];
//     uint8_t control_buffer[RTP_BUFFER_SIZE];
//     uint8_t timing_buffer[RTP_BUFFER_SIZE];
// } rtp_receiver_t;

// /**
//  * @brief Create RTP receiver
//  * Opens UDP sockets on specified ports and registers callbacks.
//  *
//  * @param receiver pre-allocated RTP receiver structure (user-managed memory)
//  * @param config receiver configuration
//  * @return 0 on success, negative on error
//  */
// int rtp_receiver_create(rtp_receiver_t *receiver, const rtp_receiver_config_t *config);

// /**
//  * @brief Poll RTP receiver for incoming packets
//  * Should be called regularly in event loop.
//  *
//  * @param receiver RTP receiver
//  * @param timeout_ms milliseconds to wait for packets (0 = non-blocking)
//  * @return 0 on success, negative on error
//  */
// int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms);

// /**
//  * @brief Close RTP receiver and sockets
//  * @param receiver RTP receiver
//  */
// void rtp_receiver_close(rtp_receiver_t *receiver);

// /**
//  * @brief Parse RTP packet header
//  * @param data raw packet data
//  * @param len packet length
//  * @param packet output parsed packet
//  * @return 0 on success, negative on error
//  */
// int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet);

// #endif // RTP_H
