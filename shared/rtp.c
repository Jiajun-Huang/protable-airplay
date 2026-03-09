#include "rtp.h"

#include <stdio.h>
#include <string.h>

static void rtp_log_payload_preview(const uint8_t *payload, size_t payload_len)
{
    size_t preview = payload_len < 32 ? payload_len : 32;
    size_t i;

    printf("[rtp] audio payload preview (%zu bytes): ", preview);
    for (i = 0; i < preview; i++)
        printf("%02X ", payload[i]);
    printf("\n");
}

int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet)
{
    size_t header_size;

    if (!data || !packet || len < 12)
        return -1;

    memset(packet, 0, sizeof(*packet));

    packet->header.version = (uint8_t)((data[0] >> 6) & 0x03);
    packet->header.padding = (uint8_t)((data[0] >> 5) & 0x01);
    packet->header.extension = (uint8_t)((data[0] >> 4) & 0x01);
    packet->header.csrc_count = (uint8_t)(data[0] & 0x0F);
    packet->header.marker = (uint8_t)((data[1] >> 7) & 0x01);
    packet->header.payload_type = (uint8_t)(data[1] & 0x7F);
    packet->header.sequence = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
    packet->header.timestamp = ((uint32_t)data[4] << 24) |
                               ((uint32_t)data[5] << 16) |
                               ((uint32_t)data[6] << 8) |
                               (uint32_t)data[7];
    packet->header.ssrc = ((uint32_t)data[8] << 24) |
                          ((uint32_t)data[9] << 16) |
                          ((uint32_t)data[10] << 8) |
                          (uint32_t)data[11];

    if (packet->header.version != 2)
        return -1;

    header_size = 12 + ((size_t)packet->header.csrc_count * 4);
    if (header_size > len)
        return -1;

    if (packet->header.extension)
    {
        size_t ext_off = header_size;
        uint16_t ext_len_words;
        if (ext_off + 4 > len)
            return -1;

        ext_len_words = (uint16_t)(((uint16_t)data[ext_off + 2] << 8) | data[ext_off + 3]);
        header_size += 4 + ((size_t)ext_len_words * 4);
        if (header_size > len)
            return -1;
    }

    packet->payload = data + header_size;
    packet->payload_len = len - header_size;

    if (packet->header.padding && packet->payload_len > 0)
    {
        uint8_t pad = packet->payload[packet->payload_len - 1];
        if (pad > packet->payload_len)
            return -1;
        packet->payload_len -= pad;
    }

    return 0;
}

int rtp_receiver_create(rtp_receiver_t *receiver, const rtp_receiver_config_t *config)
{
    if (!receiver || !config)
        return -1;

    memset(receiver, 0, sizeof(*receiver));
    receiver->config = *config;

    printf("[rtp] Creating RTP receiver with ports: audio=%u, control=%u, timing=%u\n",
           config->audio_port, config->control_port, config->timing_port);

    if (config->audio_port > 0)
    {
        if (udp_create(&receiver->audio_socket, config->audio_port) != 0)
            goto fail;
        receiver->audio_open = 1;
        printf("[rtp] Audio socket created on port %u\n", udp_get_port(&receiver->audio_socket));
    }

    if (config->control_port > 0)
    {
        if (udp_create(&receiver->control_socket, config->control_port) != 0)
            goto fail;
        receiver->control_open = 1;
        printf("[rtp] Control socket created on port %u\n", udp_get_port(&receiver->control_socket));
    }

    if (config->timing_port > 0)
    {
        if (udp_create(&receiver->timing_socket, config->timing_port) != 0)
            goto fail;
        receiver->timing_open = 1;
        printf("[rtp] Timing socket created on port %u\n", udp_get_port(&receiver->timing_socket));
    }

    return 0;

fail:
    rtp_receiver_close(receiver);
    return -1;
}

int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms)
{
    int activity = 0;

    if (!receiver)
        return -1;

    if (receiver->audio_open && udp_poll(&receiver->audio_socket, timeout_ms) > 0)
    {
        char src_ip[64] = {0};
        uint16_t src_port = 0;
        int len = udp_receive(&receiver->audio_socket,
                              receiver->audio_buffer,
                              sizeof(receiver->audio_buffer),
                              src_ip,
                              &src_port,
                              0);
        if (len > 0)
        {
            rtp_packet_t packet;
            activity = 1;
            receiver->audio_packet_count++;

            if (rtp_parse_packet(receiver->audio_buffer, (size_t)len, &packet) == 0)
            {
                if (receiver->audio_packet_count <= 8 || (receiver->audio_packet_count % 500) == 0)
                {
                    printf("[rtp] audio pkt #%u from %s:%u seq=%u ts=%u payload=%zu\n",
                           receiver->audio_packet_count,
                           src_ip,
                           src_port,
                           packet.header.sequence,
                           packet.header.timestamp,
                           packet.payload_len);
                    if (packet.payload && packet.payload_len > 0)
                        rtp_log_payload_preview(packet.payload, packet.payload_len);
                }

                if (receiver->config.audio_cb)
                    receiver->config.audio_cb(&packet, receiver->config.user_data);
            }
        }
    }

    if (receiver->control_open && udp_poll(&receiver->control_socket, 0) > 0)
    {
        int len = udp_receive(&receiver->control_socket,
                              receiver->control_buffer,
                              sizeof(receiver->control_buffer),
                              NULL,
                              NULL,
                              0);
        if (len > 0)
        {
            activity = 1;
            if (receiver->config.control_cb)
                receiver->config.control_cb(receiver->control_buffer, (size_t)len, receiver->config.user_data);
        }
    }

    if (receiver->timing_open && udp_poll(&receiver->timing_socket, 0) > 0)
    {
        int len = udp_receive(&receiver->timing_socket,
                              receiver->timing_buffer,
                              sizeof(receiver->timing_buffer),
                              NULL,
                              NULL,
                              0);
        if (len > 0)
        {
            activity = 1;
            if (receiver->config.timing_cb)
                receiver->config.timing_cb(receiver->timing_buffer, (size_t)len, receiver->config.user_data);
        }
    }

    return activity ? 0 : -1;
}

void rtp_receiver_close(rtp_receiver_t *receiver)
{
    if (!receiver)
        return;

    if (receiver->audio_open)
    {
        udp_close(&receiver->audio_socket);
        receiver->audio_open = 0;
    }
    if (receiver->control_open)
    {
        udp_close(&receiver->control_socket);
        receiver->control_open = 0;
    }
    if (receiver->timing_open)
    {
        udp_close(&receiver->timing_socket);
        receiver->timing_open = 0;
    }
}
