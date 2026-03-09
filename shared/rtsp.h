#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "tcp_if.h"

#define RTSP_MAX_SESSIONS 32
#define RTSP_SESSION_ID_LEN 16
#define RTSP_RX_BUFFER_SIZE 8192

typedef struct rtsp_session rtsp_session_t;
typedef struct rtsp_instance rtsp_instance_t;

/**
 * @brief RTSP method types
 */
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
} rtsp_method_t;

/**
 * @brief RTSP request structure
 */

typedef struct
{
    char name[128];
    char value[256];
} rtsp_header_t;

typedef struct
{
    rtsp_method_t method;
    char uri[256];             // Request URI
    char version[32];          // RTSP version string (e.g., "RTSP/1.0")
    uint32_t cseq;             // CSeq header value
    rtsp_header_t headers[32]; // max 32 headers per request
    size_t header_count;       // actual number of headers parsed
    uint8_t *body;             // pointer into receive buffer
    size_t body_len;           // length of body in bytes
} rtsp_request_t;
struct rtsp_session
{
    tcp_client_t *client;
    char session_id[RTSP_SESSION_ID_LEN];
    uint32_t last_cseq;
    time_t created_at;
    void *user_data;
};

struct rtsp_instance
{
    tcp_socket_t tcp_server;
    void *user_data;
    uint32_t session_id_counter;
    rtsp_session_t *sessions[RTSP_MAX_SESSIONS];
    int session_count;
    size_t rx_lengths[TCP_MAX_CLIENTS];
    uint8_t rx_buffers[TCP_MAX_CLIENTS][RTSP_RX_BUFFER_SIZE];
};

int rtsp_server_create(rtsp_instance_t *instance, uint16_t port);

int rtsp_server_start(rtsp_instance_t *instance);

#endif // RTSP_SERVER_H
