// #include "rtp.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdint.h>

// int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet)
// {
//     if (!data || len < 12 || !packet)
//         return -1;

//     memset(packet, 0, sizeof(rtp_packet_t));

//     // Parse RTP header (RFC 3550)
//     uint8_t byte0 = data[0];
//     uint8_t byte1 = data[1];

//     packet->header.version = (byte0 >> 6) & 0x03;
//     packet->header.padding = (byte0 >> 5) & 0x01;
//     packet->header.extension = (byte0 >> 4) & 0x01;
//     packet->header.csrc_count = byte0 & 0x0F;
//     packet->header.marker = (byte1 >> 7) & 0x01;
//     packet->header.payload_type = byte1 & 0x7F;

//     packet->header.sequence = (uint16_t)((data[2] << 8) | data[3]);
//     packet->header.timestamp = (uint32_t)((data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]);
//     packet->header.ssrc = (uint32_t)((data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11]);

//     // Calculate header size (12 bytes + CSRC + extensions)
//     size_t header_size = 12 + (packet->header.csrc_count * 4);

//     if (packet->header.extension && len >= header_size + 4)
//     {
//         uint16_t ext_len = (uint16_t)((data[header_size + 2] << 8) | data[header_size + 3]);
//         header_size += 4 + (ext_len * 4);
//     }

//     if (header_size > len)
//         return -1;

//     // Set payload
//     packet->payload = data + header_size;
//     packet->payload_len = len - header_size;

//     // Handle padding
//     if (packet->header.padding && packet->payload_len > 0)
//     {
//         uint8_t padding_len = packet->payload[packet->payload_len - 1];
//         if (padding_len <= packet->payload_len)
//             packet->payload_len -= padding_len;
//     }

//     return 0;
// }

// int rtp_receiver_create(rtp_receiver_t *receiver, const rtp_receiver_config_t *config)
// {
//     if (!receiver || !config)
//         return -1;

//     memset(receiver, 0, sizeof(rtp_receiver_t));
//     receiver->config = *config;

//     printf("[rtp] Creating RTP receiver with ports: audio=%u, control=%u, timing=%u\n",
//            config->audio_port, config->control_port, config->timing_port);

//     // Create audio socket (embedded in structure)
//     if (config->audio_port > 0)
//     {
//         if (udp_create(&receiver->audio_socket, config->audio_port) != 0)
//         {
//             fprintf(stderr, "[rtp] Failed to create audio socket on port %u\n", config->audio_port);
//             return -1;
//         }
//         printf("[rtp] Audio socket created: sock=%p, port=%u\n",
//                (void *)(intptr_t)receiver->audio_socket.sock, receiver->audio_socket.port);
//     }

//     // Create control socket (embedded in structure)
//     if (config->control_port > 0)
//     {
//         if (udp_create(&receiver->control_socket, config->control_port) != 0)
//         {
//             fprintf(stderr, "[rtp] Failed to create control socket on port %u\n", config->control_port);
//             return -1;
//         }
//         printf("[rtp] Control socket created: sock=%p, port=%u\n",
//                (void *)(intptr_t)receiver->control_socket.sock, receiver->control_socket.port);
//     }

//     // Create timing socket (embedded in structure)
//     if (config->timing_port > 0)
//     {
//         if (udp_create(&receiver->timing_socket, config->timing_port) != 0)
//         {
//             fprintf(stderr, "[rtp] Failed to create timing socket on port %u\n", config->timing_port);
//             return -1;
//         }
//         printf("[rtp] Timing socket created: sock=%p, port=%u\n",
//                (void *)(intptr_t)receiver->timing_socket.sock, receiver->timing_socket.port);
//     }

//     return 0;
// }

// int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms)
// {
//     if (!receiver)
//         return -1;

//     rtp_receiver_t *impl = (rtp_receiver_t *)receiver;
//     int activity = 0;

//     // Poll audio socket (only if it was successfully created)
//     if (impl->audio_socket.sock)
//     {
//         ssize_t len = udp_receive(&impl->audio_socket, impl->audio_buffer, sizeof(impl->audio_buffer), timeout_ms);
//         if (len > 0)
//         {
//             static int audio_pkt_count = 0;
//             if (++audio_pkt_count <= 5 || audio_pkt_count % 1000 == 0)
//                 printf("[rtp] Received audio packet: %ld bytes\n", len);

//             if (impl->config.audio_cb)
//             {
//                 rtp_packet_t packet;
//                 if (rtp_parse_packet(impl->audio_buffer, (size_t)len, &packet) == 0)
//                 {
//                     impl->config.audio_cb(&packet, impl->config.user_data);
//                     activity = 1;
//                 }
//             }
//         }
//     }

//     // Poll control socket (only if it was successfully created)
//     if (impl->control_socket.sock)
//     {
//         ssize_t len = udp_receive(&impl->control_socket, impl->control_buffer, sizeof(impl->control_buffer), 0);
//         if (len > 0)
//         {
//             printf("[rtp] Received control packet: %ld bytes\n", len);
//             if (impl->config.control_cb)
//             {
//                 impl->config.control_cb(impl->control_buffer, (size_t)len, impl->config.user_data);
//                 activity = 1;
//             }
//         }
//     }

//     // Poll timing socket (only if it was successfully created)
//     if (impl->timing_socket.sock)
//     {
//         ssize_t len = udp_receive(&impl->timing_socket, impl->timing_buffer, sizeof(impl->timing_buffer), 0);
//         if (len > 0)
//         {
//             printf("[rtp] Received timing packet: %ld bytes\n", len);
//             if (impl->config.timing_cb)
//             {
//                 impl->config.timing_cb(impl->timing_buffer, (size_t)len, impl->config.user_data);
//                 activity = 1;
//             }
//         }
//     }

//     return activity ? 0 : -1;
// }

// void rtp_receiver_close(rtp_receiver_t *receiver)
// {
//     if (!receiver)
//         return;

//     rtp_receiver_t *impl = (rtp_receiver_t *)receiver;
//     if (impl->audio_socket.sock != 0)
//         udp_close(&impl->audio_socket);

//     if (impl->control_socket.sock != 0)
//         udp_close(&impl->control_socket);

//     if (impl->timing_socket.sock != 0)
//         udp_close(&impl->timing_socket);
// }
