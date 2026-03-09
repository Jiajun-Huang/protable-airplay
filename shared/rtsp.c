#include "rtsp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "airplay/airplay_rtsp.h"

static int ascii_ieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static rtsp_method_t parse_method(const char *method)
{
    if (ascii_ieq(method, "GET"))
        return RTSP_METHOD_GET;
    if (ascii_ieq(method, "POST"))
        return RTSP_METHOD_POST;
    if (ascii_ieq(method, "OPTIONS"))
        return RTSP_METHOD_OPTIONS;
    if (ascii_ieq(method, "ANNOUNCE"))
        return RTSP_METHOD_ANNOUNCE;
    if (ascii_ieq(method, "DESCRIBE"))
        return RTSP_METHOD_DESCRIBE;
    if (ascii_ieq(method, "SETUP"))
        return RTSP_METHOD_SETUP;
    if (ascii_ieq(method, "RECORD"))
        return RTSP_METHOD_RECORD;
    if (ascii_ieq(method, "FLUSH"))
        return RTSP_METHOD_FLUSH;
    if (ascii_ieq(method, "FLUSHBUFFERED"))
        return RTSP_METHOD_FLUSHBUFFERED;
    if (ascii_ieq(method, "PLAY"))
        return RTSP_METHOD_PLAY;
    if (ascii_ieq(method, "PAUSE"))
        return RTSP_METHOD_PAUSE;
    if (ascii_ieq(method, "TEARDOWN"))
        return RTSP_METHOD_TEARDOWN;
    if (ascii_ieq(method, "GET_PARAMETER"))
        return RTSP_METHOD_GET_PARAMETER;
    if (ascii_ieq(method, "SET_PARAMETER"))
        return RTSP_METHOD_SET_PARAMETER;
    return RTSP_METHOD_UNKNOWN;
}

static void trim_and_copy(char *dst, size_t dst_size, const char *src)
{
    const char *start;
    const char *end;
    size_t n;

    if (!dst || dst_size == 0)
        return;

    dst[0] = '\0';
    if (!src)
        return;

    start = src;
    while (*start == ' ' || *start == '\t')
        start++;

    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;

    n = (size_t)(end - start);
    if (n >= dst_size)
        n = dst_size - 1;

    if (n > 0)
        memcpy(dst, start, n);
    dst[n] = '\0';
}

static int find_headers_end(const uint8_t *data, size_t len, size_t *headers_end)
{
    size_t i;

    if (!data || !headers_end)
        return 0;

    for (i = 0; i + 3 < len; i++)
    {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
        {
            *headers_end = i + 4;
            return 1;
        }
    }

    return 0;
}

static void rtsp_log_request_preview(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return;

    size_t preview_len = len < 256 ? len : 256;
    size_t line_len = 0;
    while (line_len + 1 < preview_len)
    {
        if (data[line_len] == '\r' && data[line_len + 1] == '\n')
            break;
        line_len++;
    }

    if (line_len > 0)
    {
        char line[257];
        if (line_len >= sizeof(line))
            line_len = sizeof(line) - 1;
        memcpy(line, data, line_len);
        line[line_len] = '\0';
        printf("[RTSP] Request line preview: %s\n", line);
    }
}

static uint32_t rtsp_peek_content_length(const uint8_t *buffer, size_t headers_end)
{
    size_t pos = 0;

    while (pos + 1 < headers_end)
    {
        size_t line_end = pos;
        while (line_end + 1 < headers_end)
        {
            if (buffer[line_end] == '\r' && buffer[line_end + 1] == '\n')
                break;
            line_end++;
        }

        if (line_end + 1 >= headers_end)
            break;

        if (line_end == pos)
            break;

        {
            size_t name_len = strlen("Content-Length");
            size_t line_len = line_end - pos;
            if (line_len > name_len + 1 &&
                _strnicmp((const char *)(buffer + pos), "Content-Length", name_len) == 0)
            {
                const uint8_t *p = buffer + pos + name_len;
                while (p < buffer + line_end && (*p == ' ' || *p == '\t' || *p == ':'))
                    p++;
                return (uint32_t)strtoul((const char *)p, NULL, 10);
            }
        }

        pos = line_end + 2;
    }

    return 0;
}

/* Returns: 1=parsed one full request, 0=need more bytes, -1=malformed */
static int parse_rtsp_request(uint8_t *buffer, size_t buffer_len, rtsp_request_t *out_req, size_t *consumed)
{
    size_t headers_end = 0;
    size_t pos = 0;
    uint32_t content_length = 0;
    int first_line = 1;

    if (!buffer || !out_req || !consumed)
        return -1;

    *consumed = 0;
    memset(out_req, 0, sizeof(*out_req));

    if (!find_headers_end(buffer, buffer_len, &headers_end))
        return 0;

    // For large-body requests (e.g., artwork SET_PARAMETER), wait until body is complete
    // before full parsing/logging to avoid repeated heavy work per TCP chunk.
    content_length = rtsp_peek_content_length(buffer, headers_end);
    if (buffer_len < headers_end + (size_t)content_length)
        return 0;

    // Parse headers directly from the receive buffer using stack-only scratch space.
    while (pos < headers_end)
    {
        char line[1024];
        size_t line_len;
        size_t line_end = pos;

        // Find end of current header line.
        while (line_end + 1 < headers_end)
        {
            if (buffer[line_end] == '\r' && buffer[line_end + 1] == '\n')
                break;
            line_end++;
        }

        if (line_end + 1 >= headers_end)
            return -1;

        line_len = line_end - pos;
        if (line_len >= sizeof(line))
        {
            return -1;
        }

        memcpy(line, buffer + pos, line_len);
        line[line_len] = '\0';
        pos = line_end + 2;

        // Empty line marks end of headers.
        if (line_len == 0)
            break;

        if (first_line)
        {
            char method[64] = {0};
            char version[32] = {0};

            // Request line: METHOD URI RTSP/x.y
            if (sscanf(line, "%63s %255s %31s", method, out_req->uri, version) != 3)
                return -1;

            if (strncmp(version, "RTSP/", 5) != 0)
                return -1;

            out_req->method = parse_method(method);
            trim_and_copy(out_req->version, sizeof(out_req->version), version);
            first_line = 0;
            continue;
        }

        char *colon = strchr(line, ':');
        char header_name[128];
        char header_value[256];

        if (!colon)
            continue;

        *colon = '\0';
        colon++;

        trim_and_copy(header_name, sizeof(header_name), line);
        trim_and_copy(header_value, sizeof(header_value), colon);
        printf("[RTSP] Parsed header: '%s: %s'\n", header_name, header_value);
        if (out_req->header_count < (sizeof(out_req->headers) / sizeof(out_req->headers[0])))
        {
            rtsp_header_t *h = &out_req->headers[out_req->header_count++];
            trim_and_copy(h->name, sizeof(h->name), header_name);
            trim_and_copy(h->value, sizeof(h->value), header_value);
        }

        if (ascii_ieq(header_name, "CSeq"))
            out_req->cseq = (uint32_t)strtoul(header_value, NULL, 10);
        else if (ascii_ieq(header_name, "Content-Length"))
            content_length = (uint32_t)strtoul(header_value, NULL, 10);
    }

    out_req->body = buffer + headers_end;
    out_req->body_len = (size_t)content_length;
    *consumed = headers_end + (size_t)content_length;
    return 1;
}

static const char *method_to_str(rtsp_method_t method)
{
    switch (method)
    {
    case RTSP_METHOD_GET:
        return "GET";
    case RTSP_METHOD_POST:
        return "POST";
    case RTSP_METHOD_OPTIONS:
        return "OPTIONS";
    case RTSP_METHOD_ANNOUNCE:
        return "ANNOUNCE";
    case RTSP_METHOD_DESCRIBE:
        return "DESCRIBE";
    case RTSP_METHOD_SETUP:
        return "SETUP";
    case RTSP_METHOD_RECORD:
        return "RECORD";
    case RTSP_METHOD_FLUSH:
        return "FLUSH";
    case RTSP_METHOD_FLUSHBUFFERED:
        return "FLUSHBUFFERED";
    case RTSP_METHOD_PLAY:
        return "PLAY";
    case RTSP_METHOD_PAUSE:
        return "PAUSE";
    case RTSP_METHOD_TEARDOWN:
        return "TEARDOWN";
    case RTSP_METHOD_GET_PARAMETER:
        return "GET_PARAMETER";
    case RTSP_METHOD_SET_PARAMETER:
        return "SET_PARAMETER";
    default:
        return "UNKNOWN";
    }
}

static const char *rtsp_get_header_value(const rtsp_request_t *request, const char *name)
{
    size_t i;

    if (!request || !name)
        return NULL;

    for (i = 0; i < request->header_count; i++)
    {
        if (ascii_ieq(request->headers[i].name, name))
            return request->headers[i].value;
    }

    return NULL;
}

int rtsp_send_response(tcp_client_t *client, int status, const char *status_text,
                       uint32_t cseq, const char *extra_headers,
                       const uint8_t *body, size_t body_len)
{
    char response[4096];
    int len;

    if (!client || !status_text)
        return -1;

    // Build response headers
    len = snprintf(response, sizeof(response),
                   "RTSP/1.0 %d %s\r\n"
                   "CSeq: %u\r\n"
                   "Server: AirTunes/130.14\r\n",
                   status, status_text, cseq);

    // Add extra headers if provided
    if (extra_headers && extra_headers[0] != '\0')
    {
        int extra_len = snprintf(response + len, sizeof(response) - len, "%s", extra_headers);
        if (extra_len > 0 && (size_t)(len + extra_len) < sizeof(response))
            len += extra_len;
    }

    // Add Content-Length if there's a body
    if (body && body_len > 0)
    {
        int content_len = snprintf(response + len, sizeof(response) - len,
                                   "Content-Length: %zu\r\n", body_len);
        if (content_len > 0 && (size_t)(len + content_len) < sizeof(response))
            len += content_len;
    }

    // End headers
    if ((size_t)(len + 2) < sizeof(response))
    {
        response[len++] = '\r';
        response[len++] = '\n';
    }

    if (len >= 0 && (size_t)len < sizeof(response))
        response[len] = '\0';
    else
        response[sizeof(response) - 1] = '\0';

    printf("[RTSP] Sending response to %s:%u\n%s", client->ip, client->port, response);
    // Send headers
    if (tcp_send(client, (uint8_t *)response, len) < 0)
        return -1;

    // Send body if present
    if (body && body_len > 0)
    {
        if (tcp_send(client, body, body_len) < 0)
            return -1;
    }

    return 0;
}

static void remove_client_at(rtsp_instance_t *instance, int idx)
{
    int i;

    if (!instance || idx < 0 || idx >= instance->tcp_server.client_count)
        return;

    tcp_close_client(&instance->tcp_server.clients[idx]);

    for (i = idx; i < instance->tcp_server.client_count - 1; i++)
    {
        instance->tcp_server.clients[i] = instance->tcp_server.clients[i + 1];
        instance->rx_lengths[i] = instance->rx_lengths[i + 1];
        memcpy(instance->rx_buffers[i], instance->rx_buffers[i + 1], RTSP_RX_BUFFER_SIZE);
    }

    instance->tcp_server.client_count--;
    if (instance->tcp_server.client_count >= 0)
    {
        memset(&instance->tcp_server.clients[instance->tcp_server.client_count], 0,
               sizeof(instance->tcp_server.clients[instance->tcp_server.client_count]));
        instance->rx_lengths[instance->tcp_server.client_count] = 0;
    }
}

int rtsp_server_create(rtsp_instance_t *instance, uint16_t port)
{
    if (!instance)
        return -1;

    memset(instance, 0, sizeof(rtsp_instance_t));

    if (tcp_create_server(&instance->tcp_server, port, NULL) != 0)
    {
        printf("[RTSP] Failed to create TCP server on port %d\n", port);
        return -1;
    }

    printf("[RTSP] TCP server created on port %d\n", port);
    return 0;
}

static void rtsp_handle_request(rtsp_instance_t *instance, tcp_client_t *client,
                                const rtsp_request_t *request)
{
    int handler_rc = 0;

    printf("[RTSP] Handling request from client %s:%u\n", client->ip, client->port);
    printf("[RTSP] Method=%s URI=%s CSeq=%u BodyLen=%zu\n",
           method_to_str(request->method),
           request->uri,
           request->cseq,
           request->body_len);

    const char *session = rtsp_get_header_value(request, "Session");
    const char *transport = rtsp_get_header_value(request, "Transport");
    const char *content_type = rtsp_get_header_value(request, "Content-Type");

    if (session && session[0] != '\0')
        printf("[RTSP] Session: %s\n", session);
    if (transport && transport[0] != '\0')
        printf("[RTSP] Transport: %s\n", transport);
    if (content_type && content_type[0] != '\0')
        printf("[RTSP] Content-Type: %s\n", content_type);

    // calls
    switch (request->method)
    {
    case RTSP_METHOD_OPTIONS:
        handler_rc = airplay_rtsp_options(instance, client, request);
        break;
    case RTSP_METHOD_DESCRIBE:
        handler_rc = airplay_rtsp_describe(instance, client, request);
        break;
    case RTSP_METHOD_ANNOUNCE:
        handler_rc = airplay_rtsp_announce(instance, client, request);
        break;
    case RTSP_METHOD_POST:
        handler_rc = airplay_rtsp_post(instance, client, request);
        break;
    case RTSP_METHOD_SETUP:
        handler_rc = airplay_rtsp_setup(instance, client, request);
        break;
    case RTSP_METHOD_GET_PARAMETER:
        handler_rc = airplay_rtsp_get_parameter(instance, client, request);
        break;
    case RTSP_METHOD_SET_PARAMETER:
        handler_rc = airplay_rtsp_set_parameter(instance, client, request);
        break;
    case RTSP_METHOD_FLUSH:
        handler_rc = airplay_rtsp_flush(instance, client, request);
        break;
    case RTSP_METHOD_FLUSHBUFFERED:
        handler_rc = airplay_rtsp_flushbuffered(instance, client, request);
        break;
    case RTSP_METHOD_TEARDOWN:
        handler_rc = airplay_rtsp_teardown(instance, client, request);
        break;
    case RTSP_METHOD_PAUSE:
        handler_rc = airplay_rtsp_pause(instance, client, request);
        break;
    case RTSP_METHOD_RECORD:
        handler_rc = airplay_rtsp_record(instance, client, request);
        break;
    case RTSP_METHOD_PLAY:
        handler_rc = airplay_rtsp_play(instance, client, request);
        break;
    default:
        printf("[RTSP] No handler implemented for method %s, sending 501\n", method_to_str(request->method));
        rtsp_send_response(client, 501, "Not Implemented", request->cseq, NULL, NULL, 0);
        break;
    }

    if (handler_rc < 0)
    {
        printf("[RTSP] Handler failed for method %s, sending 500\n", method_to_str(request->method));
        rtsp_send_response(client, 500, "Internal Server Error", request->cseq, NULL, NULL, 0);
    }
}

int rtsp_is_recording(void)
{
    return airplay_rtsp_is_recording();
}

int rtsp_server_start(rtsp_instance_t *instance)
{
    tcp_client_t accepted_client;

    if (!instance)
        return -1;

    printf("[RTSP] Server started. Waiting for connections...\n");

    while (tcp_poll(&instance->tcp_server, 1000) >= 0)
    {
        // Drain all pending accepts after poll reports activity.
        while (tcp_accept(&instance->tcp_server, &accepted_client, 0) == 0)
        {
            printf("[RTSP] New client connected. Total clients: %d\n", instance->tcp_server.client_count);
        }

        if (instance->tcp_server.client_count > 0)
        {
            for (int i = 0; i < instance->tcp_server.client_count;)
            {
                tcp_client_t *client = &instance->tcp_server.clients[i];
                uint8_t recv_tmp[2048];
                int bytes_received = tcp_receive(client, recv_tmp, sizeof(recv_tmp), 0);

                if (bytes_received == 0)
                {
                    printf("[RTSP] Client disconnected: %s:%u\n", client->ip, client->port);
                    remove_client_at(instance, i);
                    continue;
                }

                if (bytes_received > 0)
                {
                    size_t cur_len = instance->rx_lengths[i];
                    size_t append_len = (size_t)bytes_received;
                    // True only when this TCP fragment starts a new RTSP message.

                    if (cur_len + append_len > RTSP_RX_BUFFER_SIZE)
                    {
                        printf("[RTSP] Buffer overflow for client %s:%u, dropping buffered data\n", client->ip, client->port);
                        instance->rx_lengths[i] = 0;
                        i++;
                        continue;
                    }

                    // received data may contain multiple RTSP requests back-to-back, so append to buffer and try parsing as many as possible.
                    memcpy(instance->rx_buffers[i] + cur_len, recv_tmp, append_len);
                    instance->rx_lengths[i] = cur_len + append_len;
                    printf("[RTSP] Received data from %s:%u (%zu bytes buffered)\n",
                           client->ip, client->port, instance->rx_lengths[i]);

                    while (instance->rx_lengths[i] > 0)
                    {
                        rtsp_request_t parsed;
                        size_t consumed = 0;
                        int rc = parse_rtsp_request(instance->rx_buffers[i], instance->rx_lengths[i], &parsed, &consumed);

                        if (rc == 0)
                            // Full body not received yet; keep buffering and retry next network cycle.
                            break; // Need more bytes

                        if (rc < 0)
                        {
                            printf("[RTSP] Malformed RTSP request from %s:%u, clearing buffer\n", client->ip, client->port);
                            instance->rx_lengths[i] = 0;
                            break;
                        }
                        rtsp_handle_request(instance, client, &parsed);

                        // Remove parsed request from buffer and try parsing next one if present.
                        // A single TCP read can contain multiple RTSP requests back-to-back.
                        // After handling one request, this keeps the next request (or partial next request) in place for the next parse loop iteration.
                        if (consumed < instance->rx_lengths[i])
                        {
                            memmove(instance->rx_buffers[i],
                                    instance->rx_buffers[i] + consumed,
                                    instance->rx_lengths[i] - consumed);
                        }

                        instance->rx_lengths[i] -= consumed;
                    }
                }

                i++;
            }
        }
    }

    return -1;
}