// #ifndef RTSP_SERVER_H
// #define RTSP_SERVER_H

// #include <stdint.h>
// #include <stddef.h>
// #include <time.h>
// #include "tcp_if.h"

// /**
//  * @brief RTSP protocol abstraction for AirPlay/RAOP
//  * Platform-independent RTSP server implementation.
//  * Uses TCP interface for socket operations.
//  */

// typedef struct rtsp_instance rtsp_instance_t;
// typedef struct rtsp_session rtsp_session_t;

// /**
//  * @brief RTSP method types
//  */
// typedef enum
// {
//     RTSP_METHOD_UNKNOWN = 0,
//     RTSP_METHOD_GET,
//     RTSP_METHOD_POST,
//     RTSP_METHOD_OPTIONS,
//     RTSP_METHOD_ANNOUNCE,
//     RTSP_METHOD_DESCRIBE,
//     RTSP_METHOD_SETUP,
//     RTSP_METHOD_RECORD,
//     RTSP_METHOD_FLUSH,
//     RTSP_METHOD_FLUSHBUFFERED,
//     RTSP_METHOD_PLAY,
//     RTSP_METHOD_PAUSE,
//     RTSP_METHOD_TEARDOWN,
//     RTSP_METHOD_GET_PARAMETER,
//     RTSP_METHOD_SET_PARAMETER,
// } rtsp_method_t;

// /**
//  * @brief RTSP request structure
//  */
// typedef struct
// {
//     rtsp_method_t method;
//     char uri[512];             // Request URI (e.g., "rtsp://host:port/")
//     char session_id[128];      // RTSP Session ID (if present)
//     uint32_t cseq;             // Command sequence number
//     char content_type[128];    // Content-Type header
//     char user_agent[256];      // User-Agent header
//     char transport[256];       // Transport header
//     char rtp_info[256];        // RTP-Info header
//     char range[128];           // Range header
//     char apple_challenge[256]; // Apple-Challenge header (base64)
//     uint32_t content_length;
//     uint8_t *body;   // Request body/payload
//     size_t body_len; // Body length
// } rtsp_request_t;

// /**
//  * @brief Callback when RTSP request is received
//  * Handler must parse request and send appropriate RTSP response via rtsp_send_response
//  *
//  * @param instance RTSP instance
//  * @param session session associated with client
//  * @param request parsed RTSP request
//  * @param user_data context pointer
//  */
// typedef void (*rtsp_request_callback)(rtsp_instance_t *instance, rtsp_session_t *session,
//                                       const rtsp_request_t *request, void *user_data);

// /**
//  * @brief Create an RTSP server instance
//  * Server binds to specified port and listens for connections.
//  *
//  * @param port RTSP port (typically 554, AirPlay uses 5000)
//  * @param request_cb callback when RTSP requests are received
//  * @param user_data context pointer for callbacks
//  * @return allocated RTSP instance, or NULL on error
//  */
// rtsp_instance_t *rtsp_create(uint16_t port, rtsp_request_callback request_cb, void *user_data);

// /**
//  * @brief Poll RTSP server for activity
//  * Should be called regularly in event loop
//  *
//  * @param instance RTSP instance
//  * @param block_ms milliseconds to block waiting for activity
//  * @return 0 on success, negative on error
//  */
// int rtsp_poll(rtsp_instance_t *instance, int block_ms);

// /**
//  * @brief Close RTSP server and all sessions
//  * @param instance RTSP instance
//  */
// void rtsp_close(rtsp_instance_t *instance);

// /**
//  * @brief Send RTSP response to a client
//  * Automatically formats response header with CSeq and other fields.
//  *
//  * @param instance RTSP instance
//  * @param session client session
//  * @param status RTSP status code (e.g., 200, 404, 500)
//  * @param status_text status text (e.g., "OK", "Not Found")
//  * @param content_type Content-Type header, or NULL for none
//  * @param body response body, or NULL for no body
//  * @param body_len body length
//  * @return number of bytes sent, 0 on error
//  */
// int rtsp_send_response(rtsp_instance_t *instance, rtsp_session_t *session,
//                        uint16_t status, const char *status_text,
//                        const char *content_type, const uint8_t *body, size_t body_len);

// int rtsp_send_response_with_headers(rtsp_instance_t *instance, rtsp_session_t *session,
//                                     uint16_t status, const char *status_text,
//                                     const char *extra_headers,
//                                     const char *content_type, const uint8_t *body, size_t body_len);

// /**
//  * @brief Create an RTSP session ID for a client
//  * Session IDs persist across multiple requests from the same client.
//  *
//  * @param instance RTSP instance
//  * @return allocated session ID string (must be freed by caller), or NULL on error
//  */
// char *rtsp_create_session_id(rtsp_instance_t *instance);

// /**
//  * @brief Create a session for a TCP client
//  * This should be called when handling SETUP request to establish a session.
//  *
//  * @param instance RTSP instance
//  * @param client TCP client socket
//  * @return session object, or NULL on error
//  */
// rtsp_session_t *rtsp_create_session_for_client(rtsp_instance_t *instance, tcp_client_t *client);

// /**
//  * @brief Get the TCP client from a session
//  *
//  * @param session RTSP session
//  * @return TCP client socket, or NULL if not found
//  */
// tcp_client_t *rtsp_get_session_client(rtsp_session_t *session);

// /**
//  * @brief Get session associated with TCP client
//  * Used to link RTSP sessions to TCP connections.
//  *
//  * @param instance RTSP instance
//  * @param client TCP client socket
//  * @return session, or NULL if not found
//  */
// rtsp_session_t *rtsp_get_session(rtsp_instance_t *instance, tcp_client_t *client);

// /**
//  * @brief Set custom data on RTSP session
//  * Used for app-specific state (e.g., audio stream info)
//  *
//  * @param session RTSP session
//  * @param user_data pointer to user data
//  */
// void rtsp_session_set_user_data(rtsp_session_t *session, void *user_data);

// /**
//  * @brief Get custom data from RTSP session
//  * @param session RTSP session
//  * @return user data pointer, NULL if not set
//  */
// void *rtsp_session_get_user_data(rtsp_session_t *session);

// #endif // RTSP_SERVER_H
