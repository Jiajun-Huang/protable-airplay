#include "protocol/rtp.h"

#include <string.h>
int rtp_parse_packet(const uint8_t *data, size_t len, rtp_packet_t *packet)
{
    size_t header_size;

    if (!data || !packet || len < 12)
        return -1;

    /* RAOP retransmit response: a four-byte wrapper followed by a full RTP packet. */
    if ((data[1] & 0x7f) == 0x56)
    {
        if (len < 16)
            return -1;
        data += 4;
        len -= 4;
    }

    memset(packet, 0, sizeof(*packet));

    packet->header.version = (uint8_t)((data[0] >> 6) & 0x03);
    packet->header.padding = (uint8_t)((data[0] >> 5) & 0x01);
    packet->header.extension = (uint8_t)((data[0] >> 4) & 0x01);
    packet->header.csrc_count = (uint8_t)(data[0] & 0x0F);
    packet->header.marker = (uint8_t)((data[1] >> 7) & 0x01);
    packet->header.payload_type = (uint8_t)(data[1] & 0x7F);
    packet->header.sequence = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
    packet->header.timestamp = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                               ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    packet->header.ssrc = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                          ((uint32_t)data[10] << 8) | (uint32_t)data[11];

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

    if (packet->header.padding)
    {
        uint8_t pad;
        if (packet->payload_len == 0)
            return -1;
        pad = packet->payload[packet->payload_len - 1];
        if (pad == 0 || pad > packet->payload_len)
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
    receiver->audio_socket = (net_socket_t)NET_SOCKET_INIT;
    receiver->control_socket = (net_socket_t)NET_SOCKET_INIT;
    receiver->timing_socket = (net_socket_t)NET_SOCKET_INIT;
    receiver->config = *config;
    if ((config->audio_port && net_udp_bind(&receiver->audio_socket, config->audio_port) != 0) ||
        (config->control_port &&
         net_udp_bind(&receiver->control_socket, config->control_port) != 0) ||
        (config->timing_port && net_udp_bind(&receiver->timing_socket, config->timing_port) != 0))
    {
        rtp_receiver_close(receiver);
        return -1;
    }
    return 0;
}

int rtp_receiver_poll(rtp_receiver_t *receiver, int timeout_ms)
{
    net_socket_t sockets[3];
    uint8_t ready[3];
    uint8_t *buffers[3];
    size_t i;
    int result;
    if (!receiver)
        return -1;
    sockets[0] = receiver->audio_socket;
    sockets[1] = receiver->control_socket;
    sockets[2] = receiver->timing_socket;
    buffers[0] = receiver->audio_buffer;
    buffers[1] = receiver->control_buffer;
    buffers[2] = receiver->timing_buffer;
    result = net_wait(sockets, 3, ready, timeout_ms);
    if (result <= 0)
        return result < 0 ? -1 : 0;
    /* Timing and sync anchors are processed before an audio burst. */
    for (size_t order = 0; order < 3; ++order)
    {
        i = (size_t[]){2, 1, 0}[order];
        unsigned batch;
        if (!ready[i])
            continue;
        /* Bound each batch so control packets and shutdown get time to run. */
        for (batch = 0; batch < 32; batch++)
        {
            net_addr_t peer = {0};
            int length = net_udp_recv(&sockets[i], buffers[i], RTP_BUFFER_SIZE, &peer, 0);
            if (length == NET_TIMEOUT)
                break;
            if (length == NET_ERROR)
                return -1;
            if (length == 0)
                continue;
            if (receiver->peer_ip[0] && strcmp(peer.ip, receiver->peer_ip) != 0)
                continue;
            if (i == 0 || (i == 1 && length >= 2 && (buffers[i][1] & 0x7f) == 0x56))
            {
                rtp_packet_t packet;
                receiver->audio_packet_count++;
                if (rtp_parse_packet(buffers[i], (size_t)length, &packet) == 0 &&
                    receiver->config.audio_cb)
                    receiver->config.audio_cb(&packet, receiver->config.user_data);
            }
            else if (i == 1 && receiver->config.control_cb)
            {
                receiver->config.control_cb(buffers[i], (size_t)length, receiver->config.user_data);
            }
            else if (i == 2 && receiver->config.timing_cb)
            {
                receiver->config.timing_cb(
                    buffers[i], (size_t)length, &peer, receiver->config.user_data);
            }
        }
    }
    return 0;
}

void rtp_receiver_close(rtp_receiver_t *receiver)
{
    if (!receiver)
        return;
    net_close(&receiver->audio_socket);
    net_close(&receiver->control_socket);
    net_close(&receiver->timing_socket);
}
