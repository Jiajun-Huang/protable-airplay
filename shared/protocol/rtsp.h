#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include "airplay/pairing.h"
#include "airplay_config.h"
#include "net.h"
#include "os.h"
#include "protocol/sdp.h"
#include "sync/ptp_sync.h"
#include <stddef.h>
#include <stdint.h>

/* Shared RTSP server and connection state. It frames plaintext or paired control
 * records, routes
 * AirPlay 1 and AirPlay 2 requests, and publishes stream snapshots. */

/* Methods recognized across the AirPlay 1 and AirPlay 2 control paths. */
typedef enum
{
    RTSP_METHOD_UNKNOWN = 0,
    RTSP_METHOD_GET,
    RTSP_METHOD_POST,
    RTSP_METHOD_OPTIONS,
    RTSP_METHOD_ANNOUNCE,
    RTSP_METHOD_DESCRIBE,
    RTSP_METHOD_SETUP,
    RTSP_METHOD_RECORD,
    RTSP_METHOD_FLUSH,
    RTSP_METHOD_FLUSHBUFFERED,
    RTSP_METHOD_PLAY,
    RTSP_METHOD_PAUSE,
    RTSP_METHOD_TEARDOWN,
    RTSP_METHOD_GET_PARAMETER,
    RTSP_METHOD_SET_PARAMETER,
    RTSP_METHOD_SETPEERS,
    RTSP_METHOD_SETRATEANCHORTIME
} rtsp_method_t;

/* One parsed RTSP header copied into bounded request storage. */
typedef struct
{
    char name[128];
    char value[256];
} rtsp_header_t;

/* Parsed request view; body points into the server's receive buffer. */
typedef struct
{
    rtsp_method_t method;
    char uri[256];
    char version[32];
    uint32_t cseq;
    rtsp_header_t headers[32];
    size_t header_count;
    uint8_t *body;
    size_t body_len;
} rtsp_request_t;

/* Per-client socket, pairing, encrypted-record, and event-channel state. */
typedef struct
{
    net_socket_t socket;
    net_addr_t peer;
    pairing_t pairing;
    uint8_t fairplay_stage;
    int encrypted, http;
    uint8_t record[PAIR_RECORD_MAX + 18];
    size_t record_used;
    net_socket_t event_listener, event_client;
} rtsp_client_t;

/* Protocol-neutral session snapshot read by the audio service. */
typedef struct
{
    sdp_session_t session;
    int has_session;
    int recording;
    float volume_db;
    unsigned generation;
    unsigned flush_generation;
    net_addr_t timing_peer;
    uint32_t timestamp_floor;
    int has_timestamp_floor, floor_exclusive;
    uint32_t buffered_flush_sequence;
    int has_buffered_flush_sequence;
    airplay_anchor_t anchor;
} rtsp_stream_state_t;

/* Caller-owned listener, clients, parser storage, and synchronized stream state. */
typedef struct rtsp_instance
{
    net_socket_t listener;
    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    size_t rx_lengths[RTSP_MAX_CLIENTS];
    uint8_t rx_buffers[RTSP_MAX_CLIENTS][RTSP_RX_BUFFER_SIZE];
    rtsp_request_t request;
    rtsp_stream_state_t stream;
    os_mutex_t state_lock;
    rtsp_client_t *stream_owner;
    char local_ip[16];
    char local_mac_hex[13];
    char device_name[64];
} rtsp_instance_t;

/* The caller owns the instance and the thread calling poll. Create before
 * starting threads; close after polling and all state readers have stopped. */
int rtsp_server_create(rtsp_instance_t *instance, uint16_t port);
/* Accept clients and process ready requests once. */
int rtsp_server_poll(rtsp_instance_t *instance, int timeout_ms);
/* Close clients, event channels, and the RTSP listener. */
void rtsp_server_close(rtsp_instance_t *instance);
/* Set the identity used by challenge responses and AirPlay 2 information. */
int rtsp_set_identity(rtsp_instance_t *instance, const char *local_ip, const char *local_mac_hex);
/* Copy a mutex-protected stream snapshot for the audio service. */
void rtsp_get_stream_state(rtsp_instance_t *instance, rtsp_stream_state_t *out);

/* Send one plaintext or paired RTSP/HTTP response to a client. */
int rtsp_send_response(rtsp_client_t *client,
                       int status,
                       const char *status_text,
                       uint32_t cseq,
                       const char *extra_headers,
                       const uint8_t *body,
                       size_t body_len);

#endif
