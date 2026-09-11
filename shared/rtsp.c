#include "rtsp.h"
#include "log.h"
#include "airplay/airplay_rtsp.h"
#include "network_util.h"

#include <stdio.h>
#include <string.h>

static rtsp_method_t parse_method(const char *method)
{
    static const struct
    {
        const char *name;
        rtsp_method_t value;
    } methods[] = {
        {"GET", RTSP_METHOD_GET}, {"POST", RTSP_METHOD_POST}, {"OPTIONS", RTSP_METHOD_OPTIONS}, {"ANNOUNCE", RTSP_METHOD_ANNOUNCE}, {"DESCRIBE", RTSP_METHOD_DESCRIBE}, {"SETUP", RTSP_METHOD_SETUP}, {"RECORD", RTSP_METHOD_RECORD}, {"FLUSH", RTSP_METHOD_FLUSH}, {"FLUSHBUFFERED", RTSP_METHOD_FLUSHBUFFERED}, {"PLAY", RTSP_METHOD_PLAY}, {"PAUSE", RTSP_METHOD_PAUSE}, {"TEARDOWN", RTSP_METHOD_TEARDOWN}, {"GET_PARAMETER", RTSP_METHOD_GET_PARAMETER}, {"SET_PARAMETER", RTSP_METHOD_SET_PARAMETER}};
    size_t i;
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
        if (net_ascii_casecmp(method, methods[i].name) == 0)
            return methods[i].value;
    return RTSP_METHOD_UNKNOWN;
}

static char *trim(char *value)
{
    char *end;
    while (*value == ' ' || *value == '\t')
        ++value;
    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
        --end;
    *end = '\0';
    return value;
}

static int parse_uint32(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    if (!*text)
        return -1;
    while (*text)
    {
        unsigned digit = (unsigned char)*text++ - (unsigned)'0';
        if (digit > 9 || value > (UINT32_MAX - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

/* A TCP receive may contain part of a request or several complete requests. */
static int parse_request(uint8_t *buffer, size_t length, rtsp_request_t *request,
                         size_t *consumed)
{
    size_t headers_end = 0, pos = 0, i;
    uint32_t content_length = 0;
    int first_line = 1, has_length = 0;
    *consumed = 0;
    for (i = 0; i + 3 < length; ++i)
    {
        if (memcmp(buffer + i, "\r\n\r\n", 4) == 0)
        {
            headers_end = i + 4;
            break;
        }
    }
    if (!headers_end)
        return length == RTSP_RX_BUFFER_SIZE ? -1 : 0;
    if (memchr(buffer, '\0', headers_end))
        return -1;

    memset(request, 0, sizeof(*request));
    while (pos < headers_end)
    {
        char line[1024];
        size_t end = pos, line_length;
        while (end + 1 < headers_end &&
               !(buffer[end] == '\r' && buffer[end + 1] == '\n'))
            ++end;
        if (end + 1 >= headers_end)
            return -1;
        line_length = end - pos;
        if (line_length >= sizeof(line))
            return -1;
        memcpy(line, buffer + pos, line_length);
        line[line_length] = '\0';
        pos = end + 2;
        if (line_length == 0)
            break;
        if (first_line)
        {
            char method[64], trailing;
            if (sscanf(line, "%63s %255s %31s %c", method, request->uri,
                       request->version, &trailing) != 3 ||
                strncmp(request->version, "RTSP/", 5) != 0)
                return -1;
            request->method = parse_method(method);
            first_line = 0;
        }
        else
        {
            char *colon = strchr(line, ':'), *name, *value;
            rtsp_header_t *header;
            if (!colon || request->header_count ==
                              sizeof(request->headers) / sizeof(request->headers[0]))
                return -1;
            *colon = '\0';
            name = trim(line);
            value = trim(colon + 1);
            header = &request->headers[request->header_count++];
            if (!*name || strlen(name) >= sizeof(header->name) ||
                strlen(value) >= sizeof(header->value))
                return -1;
            strcpy(header->name, name);
            strcpy(header->value, value);
            if (net_ascii_casecmp(name, "CSeq") == 0)
            {
                if (parse_uint32(value, &request->cseq) != 0)
                    return -1;
            }
            else if (net_ascii_casecmp(name, "Content-Length") == 0)
            {
                if (has_length || parse_uint32(value, &content_length) != 0)
                    return -1;
                has_length = 1;
            }
        }
    }
    if (first_line || content_length > RTSP_RX_BUFFER_SIZE - headers_end)
        return -1;
    if (length - headers_end < content_length)
        return 0;
    request->body = buffer + headers_end;
    request->body_len = content_length;
    *consumed = headers_end + content_length;
    return 1;
}

int rtsp_send_response(rtsp_client_t *client, int status, const char *status_text,
                       uint32_t cseq, const char *extra_headers,
                       const uint8_t *body, size_t body_len)
{
    char response[2048];
    int length, appended;
    if (!client || !status_text || (body_len && !body))
        return -1;
    length = snprintf(response, sizeof(response),
                      "RTSP/1.0 %d %s\r\nCSeq: %u\r\nServer: AirTunes/" AIRPLAY_SERVER_VERSION "\r\n%s",
                      status, status_text, (unsigned)cseq,
                      extra_headers ? extra_headers : "");
    if (length < 0 || (size_t)length >= sizeof(response))
        return -1;
    if (body_len)
    {
        appended = snprintf(response + length, sizeof(response) - (size_t)length,
                            "Content-Length: %zu\r\n", body_len);
        if (appended < 0 || (size_t)appended >= sizeof(response) - (size_t)length)
            return -1;
        length += appended;
    }
    if ((size_t)length + 2 > sizeof(response))
        return -1;
    memcpy(response + length, "\r\n", 2);
    length += 2;
    if (net_tcp_send_all(&client->socket, response, (size_t)length, 1000) != 0)
        return -1;
    if (body_len && net_tcp_send_all(&client->socket, body, body_len, 1000) != 0)
        return -1;
    return 0;
}

static int handle_request(rtsp_instance_t *instance, rtsp_client_t *client,
                          const rtsp_request_t *request)
{
    LOG_DEBUG("rtsp", "%s peer=%s cseq=%u method=%d uri=%.160s body=%zu\n",
           request->version, client->peer.ip, request->cseq, request->method,
           request->uri, request->body_len);
    switch (request->method)
    {
    case RTSP_METHOD_OPTIONS:
        return airplay_rtsp_options(instance, client, request);
    case RTSP_METHOD_DESCRIBE:
        return airplay_rtsp_describe(instance, client, request);
    case RTSP_METHOD_ANNOUNCE:
        return airplay_rtsp_announce(instance, client, request);
    case RTSP_METHOD_POST:
        return airplay_rtsp_post(instance, client, request);
    case RTSP_METHOD_SETUP:
        return airplay_rtsp_setup(instance, client, request);
    case RTSP_METHOD_GET_PARAMETER:
        return airplay_rtsp_get_parameter(instance, client, request);
    case RTSP_METHOD_SET_PARAMETER:
        return airplay_rtsp_set_parameter(instance, client, request);
    case RTSP_METHOD_FLUSH:
        return airplay_rtsp_flush(instance, client, request);
    case RTSP_METHOD_FLUSHBUFFERED:
        return airplay_rtsp_flushbuffered(instance, client, request);
    case RTSP_METHOD_TEARDOWN:
        return airplay_rtsp_teardown(instance, client, request);
    case RTSP_METHOD_PAUSE:
        return airplay_rtsp_pause(instance, client, request);
    case RTSP_METHOD_RECORD:
        return airplay_rtsp_record(instance, client, request);
    case RTSP_METHOD_PLAY:
        return airplay_rtsp_play(instance, client, request);
    default:
        return rtsp_send_response(client, 501, "Not Implemented", request->cseq, NULL, NULL, 0);
    }
}

static void close_client(rtsp_instance_t *instance, size_t index)
{
    rtsp_client_t *client = &instance->clients[index];
    os_mutex_lock(&instance->state_lock);
    if (instance->stream_owner == client)
    {
        instance->stream.recording = 0;
        instance->stream.has_session = 0;
        memset(&instance->stream.session, 0, sizeof(instance->stream.session));
        ++instance->stream.generation;
        instance->stream_owner = NULL;
    }
    os_mutex_unlock(&instance->state_lock);
    net_close(&client->socket);
    memset(&client->peer, 0, sizeof(client->peer));
    instance->rx_lengths[index] = 0;
}

int rtsp_server_create(rtsp_instance_t *instance, uint16_t port)
{
    size_t i;
    if (!instance)
        return -1;
    memset(instance, 0, sizeof(*instance));
    instance->listener = (net_socket_t)NET_SOCKET_INIT;
    for (i = 0; i < RTSP_MAX_CLIENTS; ++i)
        instance->clients[i].socket = (net_socket_t)NET_SOCKET_INIT;
    instance->stream.volume_db = AIRPLAY_DEFAULT_VOLUME_DB;
    if (os_mutex_init(&instance->state_lock) != 0)
        return -1;
    if (net_tcp_listen(&instance->listener, NULL, port) != 0)
    {
        os_mutex_deinit(&instance->state_lock);
        return -1;
    }
    return 0;
}

int rtsp_set_identity(rtsp_instance_t *instance, const char *local_ip,
                      const char *local_mac_hex)
{
    uint32_t ip;
    size_t i;
    if (!instance || !local_ip || !local_mac_hex ||
        strlen(local_ip) >= sizeof(instance->local_ip) ||
        strlen(local_mac_hex) != 12 || net_str_to_ipv4(local_ip, &ip) != 0)
        return -1;
    for (i = 0; i < 12; ++i)
    {
        char c = local_mac_hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return -1;
    }
    os_mutex_lock(&instance->state_lock);
    strcpy(instance->local_ip, local_ip);
    strcpy(instance->local_mac_hex, local_mac_hex);
    os_mutex_unlock(&instance->state_lock);
    return 0;
}

void rtsp_get_stream_state(rtsp_instance_t *instance, rtsp_stream_state_t *out)
{
    if (!instance || !out)
        return;
    os_mutex_lock(&instance->state_lock);
    *out = instance->stream;
    os_mutex_unlock(&instance->state_lock);
}

int rtsp_server_poll(rtsp_instance_t *instance, int timeout_ms)
{
    net_socket_t sockets[RTSP_MAX_CLIENTS + 1];
    uint8_t ready[RTSP_MAX_CLIENTS + 1];
    size_t i;
    int result;
    if (!instance || instance->listener.handle == UINTPTR_MAX)
        return -1;
    sockets[0] = instance->listener;
    for (i = 0; i < RTSP_MAX_CLIENTS; ++i)
        sockets[i + 1] = instance->clients[i].socket;
    result = net_wait(sockets, RTSP_MAX_CLIENTS + 1, ready, timeout_ms);
    if (result < 0)
        return -1;
    if (!result)
        return 0;

    for (i = 0; i < RTSP_MAX_CLIENTS; ++i)
    {
        int received;
        size_t *length = &instance->rx_lengths[i];
        rtsp_client_t *client = &instance->clients[i];
        if (!ready[i + 1])
            continue;
        received = net_tcp_recv(&client->socket, instance->rx_buffers[i] + *length,
                                RTSP_RX_BUFFER_SIZE - *length, 0);
        if (received == NET_TIMEOUT)
            continue;
        if (received <= 0)
        {
            close_client(instance, i);
            continue;
        }
        *length += (size_t)received;
        while (*length)
        {
            size_t consumed;
            int parsed = parse_request(instance->rx_buffers[i], *length,
                                       &instance->request, &consumed);
            if (parsed == 0)
                break;
            if (parsed < 0)
            {
                LOG_WARN("rtsp", "Invalid request from %s (buffered=%zu)\n", client->peer.ip, *length);
                rtsp_send_response(client, 400, "Bad Request", 0, NULL, NULL, 0);
                close_client(instance, i);
                break;
            }
            if (handle_request(instance, client, &instance->request) < 0)
            {
                close_client(instance, i);
                break;
            }
            *length -= consumed;
            memmove(instance->rx_buffers[i], instance->rx_buffers[i] + consumed, *length);
        }
    }
    if (ready[0])
    {
        rtsp_client_t accepted;
        accepted.socket = (net_socket_t)NET_SOCKET_INIT;
        result = net_tcp_accept(&instance->listener, &accepted.socket, &accepted.peer, 0);
        if (result == NET_TIMEOUT)
            return 0;
        if (result != 0)
            return -1;
        for (i = 0; i < RTSP_MAX_CLIENTS; ++i)
        {
            if (instance->clients[i].socket.handle == UINTPTR_MAX)
            {
                instance->clients[i] = accepted;
                instance->rx_lengths[i] = 0;
                return 0;
            }
        }
        net_close(&accepted.socket);
    }
    return 0;
}

void rtsp_server_close(rtsp_instance_t *instance)
{
    size_t i;
    if (!instance)
        return;
    for (i = 0; i < RTSP_MAX_CLIENTS; ++i)
        close_client(instance, i);
    net_close(&instance->listener);
    os_mutex_deinit(&instance->state_lock);
}
