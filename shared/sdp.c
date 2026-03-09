#include "sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Base64 decode table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int sdp_base64_decode(const char *input, uint8_t *output, size_t output_size)
{
    if (!input || !output || output_size == 0)
        return -1;

    size_t input_len = strlen(input);
    size_t output_len = 0;
    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < input_len; i++)
    {
        char c = input[i];
        if (isspace(c))
            continue;
        if (c == '=')
            break;

        const char *pos = strchr(base64_chars, c);
        if (!pos)
            return -1;

        buffer = (buffer << 6) | (uint32_t)(pos - base64_chars);
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            if (output_len >= output_size)
                return -1;
            output[output_len++] = (uint8_t)(buffer >> bits);
            buffer &= (1U << bits) - 1;
        }
    }

    return (int)output_len;
}

static const char *sdp_get_line_value(const char *line)
{
    const char *eq = strchr(line, '=');
    if (eq)
        return eq + 1;
    const char *colon = strchr(line, ':');
    if (colon)
        return colon + 1;
    return line;
}

int sdp_parse(const uint8_t *sdp_data, size_t sdp_len, sdp_session_t *session)
{
    if (!sdp_data || sdp_len == 0 || !session)
        return -1;

    memset(session, 0, sizeof(sdp_session_t));

    // Set defaults
    session->sample_rate = 44100;
    session->channels = 2;
    session->bits_per_sample = 16;
    session->payload_type = 96;

    // Make null-terminated copy for parsing
    char *sdp_text = (char *)malloc(sdp_len + 1);
    if (!sdp_text)
        return -1;
    memcpy(sdp_text, sdp_data, sdp_len);
    sdp_text[sdp_len] = '\0';

    // Parse line by line
    char *line = strtok(sdp_text, "\r\n");
    while (line)
    {
        if (strlen(line) < 2)
        {
            line = strtok(NULL, "\r\n");
            continue;
        }

        char type = line[0];
        const char *value = sdp_get_line_value(line);

        switch (type)
        {
        case 'm': // Media description (e.g., "m=audio 0 RTP/AVP 96")
        {
            char media_type[32];
            unsigned int port;
            if (sscanf_s(value, "%31s %u", media_type, (unsigned int)sizeof(media_type), &port) >= 2)
            {
                if (strcmp(media_type, "audio") == 0)
                {
                    // Extract payload type
                    const char *pt = strrchr(value, ' ');
                    if (pt)
                        session->payload_type = (uint8_t)atoi(pt + 1);
                }
            }
            break;
        }

        case 'a': // Attribute (most important for AirPlay)
        {
            // a=rtpmap:96 AppleLossless/44100/2
            if (strncmp(value, "rtpmap:", 7) == 0)
            {
                char codec_name[64];
                unsigned int rate, channels;
                if (sscanf_s(value + 7, "%*d %63[^/]/%u/%u", codec_name, (unsigned int)sizeof(codec_name), &rate, &channels) >= 3)
                {
                    if (strstr(codec_name, "AppleLossless") || strstr(codec_name, "ALAC"))
                    {
                        session->codec = SDP_CODEC_ALAC;
                        session->sample_rate = rate;
                        session->channels = (uint16_t)channels;
                    }
                    else if (strstr(codec_name, "AAC"))
                    {
                        session->codec = SDP_CODEC_AAC;
                        session->sample_rate = rate;
                        session->channels = (uint16_t)channels;
                    }
                    else if (strstr(codec_name, "L16") || strstr(codec_name, "PCM"))
                    {
                        session->codec = SDP_CODEC_PCM;
                        session->sample_rate = rate;
                        session->channels = (uint16_t)channels;
                        session->bits_per_sample = 16;
                    }
                }
            }
            // a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100
            else if (strncmp(value, "fmtp:", 5) == 0)
            {
                const char *params = strchr(value + 5, ' ');
                if (params)
                {
                    params++;
                    // Parse ALAC configuration
                    unsigned int vals[12];
                    int count = sscanf_s(params, "%u %u %u %u %u %u %u %u %u %u %u",
                                         &vals[0], &vals[1], &vals[2], &vals[3], &vals[4],
                                         &vals[5], &vals[6], &vals[7], &vals[8], &vals[9], &vals[10]);
                    if (count >= 3)
                    {
                        session->alac_fmtp_count = (size_t)count;
                        for (int i = 0; i < count && i < 12; i++)
                            session->alac_fmtp[i] = vals[i];

                        session->frames_per_packet = vals[0];
                        session->bits_per_sample = (uint16_t)vals[2];
                        if (count >= 7)
                            session->channels = (uint16_t)vals[6];
                        if (count >= 11)
                            session->sample_rate = vals[10];
                    }

                    // Keep legacy compact config for compatibility paths
                    session->alac_config_len = 0;
                    if (count >= 8)
                    {
                        for (int i = 0; i < count && i < 12 && session->alac_config_len < sizeof(session->alac_config); i++)
                            session->alac_config[session->alac_config_len++] = (uint8_t)(vals[i] & 0xFF);
                    }
                }
            }
            // a=rsaaeskey:base64_key
            else if (strncmp(value, "rsaaeskey:", 10) == 0)
            {
                int key_len = sdp_base64_decode(value + 10,
                                                session->aes_key_encrypted,
                                                sizeof(session->aes_key_encrypted));
                if (key_len > 0)
                    session->aes_key_encrypted_len = (size_t)key_len;
                session->has_encryption = 1;
            }
            // a=aesiv:base64_iv
            else if (strncmp(value, "aesiv:", 6) == 0)
            {
                sdp_base64_decode(value + 6, session->aes_iv, sizeof(session->aes_iv));
                session->has_encryption = 1;
            }
            break;
        }

        default:
            break;
        }

        line = strtok(NULL, "\r\n");
    }

    free(sdp_text);

    // If codec still unknown, assume ALAC
    if (session->codec == SDP_CODEC_UNKNOWN)
        session->codec = SDP_CODEC_ALAC;

    // Sanitize ALAC/PCM essentials to known-good ranges used by AirPlay senders.
    if (session->frames_per_packet == 0 || session->frames_per_packet > 8192)
        session->frames_per_packet = 352;
    if (session->channels == 0 || session->channels > 2)
        session->channels = 2;
    if (session->bits_per_sample != 16 && session->bits_per_sample != 24)
        session->bits_per_sample = 16;

    printf("[sdp] Parsed session: codec=%d, rate=%u, channels=%u, bits=%u, frames=%u\n",
           session->codec, session->sample_rate, session->channels,
           session->bits_per_sample, session->frames_per_packet);

    return 0;
}
