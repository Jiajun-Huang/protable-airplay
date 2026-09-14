#include "rtsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define FAKE_CLIENTS (RTSP_MAX_CLIENTS + 4)
#define LISTENER_HANDLE 1000

typedef struct {
    uint8_t input[RTSP_RX_BUFFER_SIZE + 1024];
    size_t input_length;
    char output[16384];
    size_t output_length;
    int pending_accept;
    int eof;
    int recv_error;
    int recv_timeout;
    int send_error;
    int closed;
} fake_client_t;

static rtsp_instance_t server;
static fake_client_t clients[FAKE_CLIENTS];
static int client_count;
static int mutex_depth;
static int mutex_storage;
static int wait_error;
static int last_wait_timeout;

static fake_client_t *fake_client(net_socket_t *socket)
{
    CHECK(socket->handle >= 1 && socket->handle <= FAKE_CLIENTS);
    return &clients[socket->handle - 1];
}

int os_mutex_init(os_mutex_t *mutex)
{
    mutex->handle = &mutex_storage;
    return 0;
}

void os_mutex_lock(os_mutex_t *mutex)
{
    CHECK(mutex->handle == &mutex_storage);
    CHECK(mutex_depth == 0);
    mutex_depth = 1;
}

void os_mutex_unlock(os_mutex_t *mutex)
{
    CHECK(mutex->handle == &mutex_storage);
    CHECK(mutex_depth == 1);
    mutex_depth = 0;
}

void os_mutex_deinit(os_mutex_t *mutex)
{
    CHECK(mutex_depth == 0);
    mutex->handle = NULL;
}

int net_tcp_listen(net_socket_t *socket, const char *ip, uint16_t port)
{
    (void)ip;
    socket->handle = LISTENER_HANDLE;
    socket->port = port ? port : 5000;
    return 0;
}

int net_tcp_accept(net_socket_t *listener, net_socket_t *client,
                    net_addr_t *peer, int timeout_ms)
{
    CHECK(listener->handle == LISTENER_HANDLE);
    CHECK(timeout_ms == 0);
    CHECK(mutex_depth == 0);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].pending_accept) {
            clients[i].pending_accept = 0;
            client->handle = (uintptr_t)i + 1;
            client->port = 5000;
            strcpy(peer->ip, "192.0.2.20");
            peer->port = (uint16_t)(40000 + i);
            return 0;
        }
    }
    return NET_TIMEOUT;
}

int net_wait(const net_socket_t *sockets, size_t count, uint8_t *ready, int timeout_ms)
{
    int result = 0;
    CHECK(mutex_depth == 0);
    last_wait_timeout = timeout_ms;
    if (wait_error)
        return NET_ERROR;
    memset(ready, 0, count);
    for (size_t i = 0; i < count; ++i) {
        if (sockets[i].handle == UINTPTR_MAX)
            continue;
        if (sockets[i].handle == LISTENER_HANDLE) {
            for (int j = 0; j < client_count; ++j)
                if (clients[j].pending_accept)
                    ready[i] = 1;
        } else {
            CHECK(sockets[i].handle >= 1 && sockets[i].handle <= FAKE_CLIENTS);
            fake_client_t *client = &clients[sockets[i].handle - 1];
            CHECK(!client->closed);
            ready[i] = client->input_length || client->eof ||
                       client->recv_error || client->recv_timeout;
        }
        result += ready[i];
    }
    return result;
}

int net_tcp_recv(net_socket_t *socket, void *data, size_t capacity, int timeout_ms)
{
    fake_client_t *client = fake_client(socket);
    size_t length;
    CHECK(mutex_depth == 0);
    CHECK(timeout_ms == 0);
    CHECK(capacity > 0);
    if (client->recv_timeout) {
        client->recv_timeout = 0;
        return NET_TIMEOUT;
    }
    if (client->recv_error)
        return NET_ERROR;
    if (client->eof)
        return 0;
    length = client->input_length < capacity ? client->input_length : capacity;
    if (!length)
        return NET_TIMEOUT;
    memcpy(data, client->input, length);
    client->input_length -= length;
    memmove(client->input, client->input + length, client->input_length);
    return (int)length;
}

int net_tcp_send_all(net_socket_t *socket, const void *data, size_t length, int timeout_ms)
{
    fake_client_t *client = fake_client(socket);
    CHECK(mutex_depth == 0);
    CHECK(timeout_ms > 0);
    if (client->send_error)
        return client->send_error;
    CHECK(client->output_length + length < sizeof(client->output));
    memcpy(client->output + client->output_length, data, length);
    client->output_length += length;
    client->output[client->output_length] = '\0';
    return 0;
}

void net_close(net_socket_t *socket)
{
    CHECK(mutex_depth == 0);
    if (socket->handle != UINTPTR_MAX && socket->handle != LISTENER_HANDLE)
        fake_client(socket)->closed = 1;
    *socket = (net_socket_t)NET_SOCKET_INIT;
}

static void fixture(void)
{
    memset(clients, 0, sizeof(clients));
    client_count = 0;
    mutex_depth = 0;
    wait_error = 0;
    CHECK(rtsp_server_create(&server, 5000) == 0);
    CHECK(rtsp_set_identity(&server, "192.0.2.10", "001122AABBCC") == 0);
}

static int connect_client(void)
{
    int index = client_count++;
    CHECK(client_count <= FAKE_CLIENTS);
    clients[index].pending_accept = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(!clients[index].pending_accept);
    return index;
}

static void append(int client, const void *data, size_t length)
{
    fake_client_t *target = &clients[client];
    CHECK(target->input_length + length <= sizeof(target->input));
    memcpy(target->input + target->input_length, data, length);
    target->input_length += length;
}

static void append_text(int client, const char *text)
{
    append(client, text, strlen(text));
}

static void queue_request(int client, const char *method, unsigned cseq,
                           const char *headers, const void *body, size_t body_length)
{
    char request[1024];
    int length = snprintf(request, sizeof(request),
        "%s * RTSP/1.0\r\nCSeq: %u\r\n%sContent-Length: %zu\r\n\r\n",
        method, cseq, headers ? headers : "", body_length);
    CHECK(length > 0 && (size_t)length < sizeof(request));
    append(client, request, (size_t)length);
    if (body_length)
        append(client, body, body_length);
}

static void request(int client, const char *method, unsigned cseq)
{
    queue_request(client, method, cseq, NULL, NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
}

static rtsp_stream_state_t state(void)
{
    rtsp_stream_state_t result;
    rtsp_get_stream_state(&server, &result);
    return result;
}

static void announce(int client)
{
    const char *sdp = "v=0\r\nm=audio 0 RTP/AVP 96\r\na=rtpmap:96 L16/44100/2\r\n";
    queue_request(client, "ANNOUNCE", 10, "Content-Type: application/sdp\r\n", sdp, strlen(sdp));
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().has_session);
}

static int occurrences(const char *text, const char *needle)
{
    int count = 0;
    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += strlen(needle);
    }
    return count;
}

static void test_fragmented_headers_and_pipeline(void)
{
    fixture();
    int client = connect_client();
    append_text(client, "OPTIONS * RTSP/1.0\r\nCS");
    CHECK(rtsp_server_poll(&server, 30) == 0);
    CHECK(last_wait_timeout == 30);
    CHECK(clients[client].output_length == 0);
    append_text(client, "eq: 7\r\n\r\nOPTIONS * RTSP/1.0\r\nCSeq: 8\r\n\r\n");
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(occurrences(clients[client].output, "RTSP/1.0 200 OK") == 2);
    const char *first = strstr(clients[client].output, "CSeq: 7\r\n");
    const char *second = strstr(clients[client].output, "CSeq: 8\r\n");
    CHECK(first && second && first < second);
    CHECK(!clients[client].closed);
    rtsp_server_close(&server);
}

static void test_fragmented_body_then_record(void)
{
    const char *sdp = "v=0\r\nm=audio 0 RTP/AVP 96\r\na=rtpmap:96 L16/44100/2\r\n";
    char header[256];
    size_t split = strlen(sdp) / 2;
    fixture();
    int client = connect_client();
    snprintf(header, sizeof(header),
             "ANNOUNCE * RTSP/1.0\r\nCSeq: 31\r\nContent-Length: %zu\r\n\r\n", strlen(sdp));
    append_text(client, header);
    append(client, sdp, split);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[client].output_length == 0);
    CHECK(!state().has_session);
    append(client, sdp + split, strlen(sdp) - split);
    queue_request(client, "RECORD", 32, NULL, NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().recording && state().has_session);
    CHECK(state().session.codec == SDP_CODEC_PCM);
    CHECK(state().generation == 1);
    CHECK(occurrences(clients[client].output, "RTSP/1.0 200 OK") == 2);
    CHECK(strstr(clients[client].output, "CSeq: 31\r\n"));
    CHECK(strstr(clients[client].output, "CSeq: 32\r\n"));
    rtsp_server_close(&server);
}

static void test_session_owner_and_controls(void)
{
    fixture();
    int owner = connect_client();
    int other = connect_client();
    announce(owner);
    request(owner, "RECORD", 11);
    CHECK(state().recording);
    unsigned generation = state().generation;
    clients[other].eof = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[other].closed && state().recording);
    CHECK(state().generation == generation);

    other = connect_client();
    request(other, "RECORD", 12);
    CHECK(strstr(clients[other].output, "RTSP/1.0 455 "));
    request(other, "TEARDOWN", 13);
    request(other, "PAUSE", 14);
    request(other, "FLUSH", 15);
    CHECK(state().recording && state().has_session);
    CHECK(state().flush_generation == 0);
    request(owner, "PAUSE", 16);
    CHECK(!state().recording && state().has_session);
    request(owner, "RECORD", 17);
    CHECK(state().recording);
    request(owner, "FLUSH", 18);
    CHECK(state().recording && state().flush_generation == 1);
    request(owner, "FLUSHBUFFERED", 19);
    CHECK(state().flush_generation == 2);
    clients[owner].eof = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[owner].closed);
    CHECK(!state().recording && !state().has_session);
    CHECK(state().generation == generation + 1);
    CHECK(state().session.sample_rate == 0);
    rtsp_server_close(&server);
}

static void test_new_announce_changes_owner(void)
{
    fixture();
    int first = connect_client();
    int second = connect_client();
    announce(first);
    request(first, "RECORD", 11);
    unsigned generation = state().generation;
    announce(second);
    CHECK(state().generation == generation + 1);
    CHECK(!state().recording && state().has_session);
    clients[first].eof = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().has_session);
    request(second, "RECORD", 12);
    CHECK(state().recording);
    request(second, "TEARDOWN", 13);
    CHECK(!state().recording && !state().has_session);
    rtsp_server_close(&server);
}

static void test_missing_and_invalid_sdp(void)
{
    fixture();
    int client = connect_client();
    request(client, "RECORD", 1);
    CHECK(strstr(clients[client].output, "RTSP/1.0 455 "));
    CHECK(!state().recording);
    queue_request(client, "ANNOUNCE", 2, NULL, "invalid", 7);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(strstr(clients[client].output, "RTSP/1.0 400 Bad Request"));
    CHECK(!state().has_session);
    CHECK(!clients[client].closed);
    rtsp_server_close(&server);
}

static void test_timeout_and_failures(void)
{
    fixture();
    int client = connect_client();
    CHECK(rtsp_server_poll(&server, 5) == 0);
    CHECK(!clients[client].closed);
    clients[client].recv_timeout = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(!clients[client].closed);
    request(client, "OPTIONS", 1);
    CHECK(strstr(clients[client].output, "RTSP/1.0 200 OK"));
    clients[client].recv_error = 1;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[client].closed);
    rtsp_server_close(&server);

    for (int error = NET_ERROR; error >= NET_TIMEOUT; --error) {
        fixture();
        client = connect_client();
        announce(client);
        request(client, "RECORD", 11);
        clients[client].send_error = error;
        request(client, "OPTIONS", 12);
        CHECK(clients[client].closed);
        CHECK(!state().has_session && !state().recording);
        rtsp_server_close(&server);
    }

    fixture();
    wait_error = 1;
    CHECK(rtsp_server_poll(&server, 0) == -1);
    rtsp_server_close(&server);
}

static void test_malformed_lengths_and_headers(void)
{
    const char *bad[] = {
        "-1", "+1", "4294967296", "abc", "12junk", ""
    };
    char request_text[256];
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fixture();
        int client = connect_client();
        snprintf(request_text, sizeof(request_text),
                 "SET_PARAMETER * RTSP/1.0\r\nCSeq: 1\r\nContent-Length: %s\r\n\r\n", bad[i]);
        append_text(client, request_text);
        CHECK(rtsp_server_poll(&server, 0) == 0);
        CHECK(clients[client].closed);
        CHECK(strstr(clients[client].output, "RTSP/1.0 400 Bad Request"));
        rtsp_server_close(&server);
    }

    fixture();
    int client = connect_client();
    snprintf(request_text, sizeof(request_text),
             "SET_PARAMETER * RTSP/1.0\r\nContent-Length: %u\r\n\r\n", (unsigned)RTSP_RX_BUFFER_SIZE);
    append_text(client, request_text);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[client].closed);
    rtsp_server_close(&server);

    fixture();
    client = connect_client();
    append_text(client, "OPTIONS * RTSP/1.0\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n");
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[client].closed);
    rtsp_server_close(&server);

    fixture();
    client = connect_client();
    memset(clients[client].input, 'A', RTSP_RX_BUFFER_SIZE);
    clients[client].input_length = RTSP_RX_BUFFER_SIZE;
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(clients[client].closed);
    rtsp_server_close(&server);
}

static void test_volume_and_large_artwork(void)
{
    fixture();
    int client = connect_client();
    const char *volume = "volume: -8.5\r\n";
    queue_request(client, "SET_PARAMETER", 1, "Content-Type: text/parameters\r\n", volume, strlen(volume));
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().volume_db == -8.5f);
    const char *nan_volume = "volume: nan\r\n";
    queue_request(client, "SET_PARAMETER", 2, "Content-Type: text/parameters\r\n", nan_volume, strlen(nan_volume));
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().volume_db == -8.5f);

    char header[256];
    size_t artwork_length = RTSP_RX_BUFFER_SIZE - 512;
    snprintf(header, sizeof(header),
             "SET_PARAMETER * RTSP/1.0\r\nCSeq: 3\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
             artwork_length);
    append_text(client, header);
    size_t body_start = clients[client].input_length;
    memset(clients[client].input + body_start, 0, artwork_length);
    memcpy(clients[client].input + body_start, "volume: 0.0", 11);
    clients[client].input_length += artwork_length;
    queue_request(client, "OPTIONS", 4, NULL, NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(!clients[client].closed);
    CHECK(strstr(clients[client].output, "CSeq: 3\r\n"));
    CHECK(strstr(clients[client].output, "CSeq: 4\r\n"));
    CHECK(state().volume_db == -8.5f);
    rtsp_server_close(&server);
}

static void test_identity_and_client_limit(void)
{
    fixture();
    CHECK(rtsp_set_identity(&server, "999.0.2.10", "001122AABBCC") == -1);
    CHECK(rtsp_set_identity(&server, "192.0.2.10", "001122AABBCZ") == -1);
    CHECK(rtsp_set_identity(&server, "192.0.2.10", "001122AABB") == -1);
    CHECK(strcmp(server.local_ip, "192.0.2.10") == 0);
    CHECK(strcmp(server.local_mac_hex, "001122AABBCC") == 0);
    for (int i = 0; i < RTSP_MAX_CLIENTS; ++i)
        CHECK(!clients[connect_client()].closed);
    int extra = connect_client();
    CHECK(clients[extra].closed);
    request(0, "OPTIONS", 1);
    CHECK(!clients[0].closed);
    rtsp_server_close(&server);
}

static void test_transport_and_timestamp_boundaries(void)
{
    fixture();
    int client = connect_client();
    announce(client);
    unsigned generation = state().generation;
    queue_request(client, "SETUP", 11,
        "Transport: RTP/AVP/UDP;unicast;mode=record;control_port=55000;timing_port=55001\r\n", NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().timing_peer.port == 55001 && state().generation == generation + 1);
    CHECK(strcmp(state().timing_peer.ip, "192.0.2.20") == 0);
    queue_request(client, "RECORD", 12, "RTP-Info: seq=65535;rtptime=4294967295\r\n", NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().recording && state().has_timestamp_floor && state().timestamp_floor == UINT32_MAX);
    CHECK(!state().floor_exclusive);
    queue_request(client, "FLUSH", 13, "RTP-Info: seq=0;rtptime=0\r\n", NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0);
    CHECK(state().has_timestamp_floor && state().timestamp_floor == 0 && state().floor_exclusive);
    unsigned flush_generation = state().flush_generation;
    queue_request(client, "FLUSH", 14, "RTP-Info: rtptime=4294967296\r\n", NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0 && state().flush_generation == flush_generation);
    CHECK(strstr(clients[client].output, "400 Bad Request"));
    queue_request(client, "SETUP", 15, "Transport: RTP/AVP/UDP;timing_port=65536\r\n", NULL, 0);
    CHECK(rtsp_server_poll(&server, 0) == 0 && state().timing_peer.port == 55001);
    CHECK(strstr(clients[client].output, "461 Unsupported Transport"));
    rtsp_server_close(&server);
}

int main(void)
{
    test_fragmented_headers_and_pipeline();
    test_fragmented_body_then_record();
    if (RTSP_MAX_CLIENTS >= 2) {
        test_session_owner_and_controls();
        test_new_announce_changes_owner();
    }
    test_missing_and_invalid_sdp();
    test_timeout_and_failures();
    test_malformed_lengths_and_headers();
    test_volume_and_large_artwork();
    test_identity_and_client_limit();
    test_transport_and_timestamp_boundaries();
    puts("RTSP framing, state, ownership, and failure tests passed");
    return 0;
}
