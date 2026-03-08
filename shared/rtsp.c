// #include "rtsp.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>

// /**
//  * @brief RTSP server implementation (platform-independent)
//  * Uses TCP interface for socket operations.
//  */

// #define MAX_RTSP_SESSIONS 32
// #define RTSP_SESSION_ID_LEN 16

// typedef struct
// {
//     tcp_client_t *client;
//     char session_id[RTSP_SESSION_ID_LEN];
//     uint32_t last_cseq;
//     time_t created_at;
//     void *user_data;
// } rtsp_session_internal_t;

// typedef struct rtsp_instance
// {
//     tcp_socket_t *tcp_server;
//     rtsp_session_internal_t sessions[MAX_RTSP_SESSIONS];
//     int session_count;
//     rtsp_request_callback request_cb;
//     void *user_data;
//     uint32_t session_id_counter;
// } rtsp_instance_internal_t;

// static void rtsp_copy_header_value(char *dst, size_t dst_size, const char *value)
// {
//     if (!dst || dst_size == 0)
//         return;

//     while (*value == ' ' || *value == '\t')
//         value++;

//     strncpy_s(dst, dst_size, value, _TRUNCATE);
// }

// /**
//  * @brief Parse RTSP request line (e.g., "OPTIONS * RTSP/1.0")
//  */
// static rtsp_method_t rtsp_parse_method(const char *method_str)
// {
//     if (strcasecmp(method_str, "GET") == 0)
//         return RTSP_METHOD_GET;
//     if (strcasecmp(method_str, "POST") == 0)
//         return RTSP_METHOD_POST;
//     if (strcasecmp(method_str, "OPTIONS") == 0)
//         return RTSP_METHOD_OPTIONS;
//     if (strcasecmp(method_str, "ANNOUNCE") == 0)
//         return RTSP_METHOD_ANNOUNCE;
//     if (strcasecmp(method_str, "DESCRIBE") == 0)
//         return RTSP_METHOD_DESCRIBE;
//     if (strcasecmp(method_str, "SETUP") == 0)
//         return RTSP_METHOD_SETUP;
//     if (strcasecmp(method_str, "RECORD") == 0)
//         return RTSP_METHOD_RECORD;
//     if (strcasecmp(method_str, "FLUSH") == 0)
//         return RTSP_METHOD_FLUSH;
//     if (strcasecmp(method_str, "FLUSHBUFFERED") == 0)
//         return RTSP_METHOD_FLUSHBUFFERED;
//     if (strcasecmp(method_str, "PLAY") == 0)
//         return RTSP_METHOD_PLAY;
//     if (strcasecmp(method_str, "PAUSE") == 0)
//         return RTSP_METHOD_PAUSE;
//     if (strcasecmp(method_str, "TEARDOWN") == 0)
//         return RTSP_METHOD_TEARDOWN;
//     if (strcasecmp(method_str, "GET_PARAMETER") == 0)
//         return RTSP_METHOD_GET_PARAMETER;
//     if (strcasecmp(method_str, "SET_PARAMETER") == 0)
//         return RTSP_METHOD_SET_PARAMETER;
//     return RTSP_METHOD_UNKNOWN;
// }

// /**
//  * @brief Parse RTSP request message
//  */
// static int rtsp_parse_request(const uint8_t *data, size_t len, rtsp_request_t *request)
// {
//     if (!data || len == 0 || !request)
//         return -1;

//     memset(request, 0, sizeof(rtsp_request_t));

//     // Find end of headers (blank line)
//     const char *text = (const char *)data;
//     const char *headers_end = strstr(text, "\r\n\r\n");
//     size_t headers_len = headers_end ? (size_t)(headers_end - text) : len;

//     if (headers_len >= len)
//         headers_end = NULL;

//     // Parse request line (first line)
//     const char *line_end = strstr(text, "\r\n");
//     if (!line_end)
//         return -1;

//     size_t line_len = line_end - text;
//     char request_line[512];
//     if (line_len >= sizeof(request_line))
//         return -1;

//     memcpy(request_line, text, line_len);
//     request_line[line_len] = '\0';

//     // Parse: <METHOD> <URI> RTSP/1.0
//     char method_str[64];
//     if (sscanf_s(request_line, "%63s %511s", method_str, (unsigned)sizeof(method_str), request->uri, (unsigned)sizeof(request->uri)) != 2)
//         return -1;

//     request->method = rtsp_parse_method(method_str);

//     // Parse headers
//     const char *header_start = line_end + 2;
//     while (header_start < (headers_end ? headers_end : (const char *)data + len))
//     {
//         line_end = strstr(header_start, "\r\n");
//         if (!line_end)
//             break;

//         line_len = line_end - header_start;
//         if (line_len == 0)
//             break;

//         char header_line[512];
//         if (line_len >= sizeof(header_line))
//         {
//             header_start = line_end + 2;
//             continue;
//         }

//         memcpy(header_line, header_start, line_len);
//         header_line[line_len] = '\0';

//         // Parse: Name: Value
//         const char *colon = strchr(header_line, ':');
//         if (colon)
//         {
//             *(char *)colon = '\0';
//             const char *name = header_line;
//             const char *value = colon + 1;

//             if (strncasecmp(name, "CSeq", 4) == 0)
//             {
//                 request->cseq = (uint32_t)atoi(value);
//             }
//             else if (strncasecmp(name, "Session", 7) == 0)
//             {
//                 rtsp_copy_header_value(request->session_id, sizeof(request->session_id), value);
//             }
//             else if (strncasecmp(name, "Content-Type", 12) == 0)
//             {
//                 rtsp_copy_header_value(request->content_type, sizeof(request->content_type), value);
//             }
//             else if (strncasecmp(name, "Content-Length", 14) == 0)
//             {
//                 request->content_length = (uint32_t)atoi(value);
//             }
//             else if (strncasecmp(name, "User-Agent", 10) == 0)
//             {
//                 rtsp_copy_header_value(request->user_agent, sizeof(request->user_agent), value);
//             }
//             else if (strncasecmp(name, "Transport", 9) == 0)
//             {
//                 rtsp_copy_header_value(request->transport, sizeof(request->transport), value);
//             }
//             else if (strncasecmp(name, "RTP-Info", 8) == 0)
//             {
//                 rtsp_copy_header_value(request->rtp_info, sizeof(request->rtp_info), value);
//             }
//             else if (strncasecmp(name, "Range", 5) == 0)
//             {
//                 rtsp_copy_header_value(request->range, sizeof(request->range), value);
//             }
//             else if (strncasecmp(name, "Apple-Challenge", 15) == 0)
//             {
//                 rtsp_copy_header_value(request->apple_challenge, sizeof(request->apple_challenge), value);
//             }
//         }

//         header_start = line_end + 2;
//     }

//     // Parse body if present
//     if (headers_end)
//     {
//         const uint8_t *body_start = (const uint8_t *)headers_end + 4;
//         size_t remaining_len = (size_t)((const uint8_t *)data + len - body_start);
//         request->body = (uint8_t *)body_start;
//         request->body_len = remaining_len;
//         if (request->content_length > 0 && request->content_length < request->body_len)
//             request->body_len = request->content_length;
//     }

//     return 0;
// }

// /**
//  * @brief TCP accept callback
//  */
// static void rtsp_tcp_accept_callback(tcp_client_t *client, const char *client_ip, uint16_t client_port, void *user_data)
// {
//     printf("[RTSP] Client connected: %s:%u\n", client_ip, client_port);
//     fflush(stdout);
//     // Session will be created on first request
// }

// /**
//  * @brief TCP receive callback
//  */
// static void rtsp_tcp_receive_callback(tcp_client_t *client, const uint8_t *data, size_t len, void *user_data)
// {
//     rtsp_instance_internal_t *instance = (rtsp_instance_internal_t *)user_data;
//     if (!instance)
//         return;

//     // Log raw request for debugging
//     static int request_count = 0;
//     request_count++;
//     printf("[RTSP] Raw request #%d (%zu bytes)\n", request_count, len);
//     if (request_count <= 10)
//     {
//         printf("[RTSP] Raw request detail #%d:\n", request_count);
//         // Print request as text (up to 500 bytes)
//         size_t print_len = len < 500 ? len : 500;
//         printf("<<<\n%.*s\n>>>\n", (int)print_len, (const char *)data);

//         // // Print hex dump (first 200 bytes)
//         // printf("[RTSP] Hex dump (first %zu bytes):\n", print_len < 200 ? print_len : 200);
//         // size_t hex_len = print_len < 200 ? print_len : 200;
//         // for (size_t i = 0; i < hex_len; i++)
//         // {
//         //     printf("%02X ", data[i]);
//         //     if ((i + 1) % 16 == 0)
//         //         printf("\n");
//         // }
//         // if (hex_len % 16 != 0)
//         //     printf("\n");
//     }
//     fflush(stdout);

//     // Find or create session for this client
//     rtsp_session_internal_t *session = NULL;
//     for (int i = 0; i < instance->session_count; i++)
//     {
//         if (instance->sessions[i].client == client)
//         {
//             session = &instance->sessions[i];
//             break;
//         }
//     }

//     // Parse RTSP request
//     rtsp_request_t request;
//     if (rtsp_parse_request(data, len, &request) != 0)
//     {
//         printf("[RTSP] Failed to parse request\n");
//         fflush(stdout);
//         return;
//     }

//     // Find existing session if client provided Session header
//     if (request.session_id[0] != '\0')
//     {
//         for (int i = 0; i < instance->session_count; i++)
//         {
//             if (strcmp(instance->sessions[i].session_id, request.session_id) == 0)
//             {
//                 session = &instance->sessions[i];
//                 break;
//             }
//         }
//     }

//     // If no session found and needed, create one (but only on SETUP)
//     // For now, we'll create a temporary pseudo-session for sessionless requests
//     rtsp_session_internal_t temp_session;
//     if (!session)
//     {
//         memset(&temp_session, 0, sizeof(temp_session));
//         temp_session.client = client;
//         temp_session.created_at = time(NULL);
//         session = &temp_session;
//     }

//     if (session)
//     {
//         session->last_cseq = request.cseq;

//         // Call request handler
//         if (instance->request_cb)
//         {
//             instance->request_cb((rtsp_instance_t *)instance, (rtsp_session_t *)session, &request, instance->user_data);
//         }
//     }
// }

// /**
//  * @brief TCP disconnect callback
//  */
// static void rtsp_tcp_disconnect_callback(tcp_client_t *client, void *user_data)
// {
//     rtsp_instance_internal_t *instance = (rtsp_instance_internal_t *)user_data;
//     if (!instance)
//         return;

//     printf("[RTSP] Client disconnected\n");
//     fflush(stdout);

//     // Remove session
//     for (int i = 0; i < instance->session_count; i++)
//     {
//         if (instance->sessions[i].client == client)
//         {
//             // Shift remaining sessions
//             for (int j = i; j < instance->session_count - 1; j++)
//             {
//                 instance->sessions[j] = instance->sessions[j + 1];
//             }
//             instance->session_count--;
//             break;
//         }
//     }
// }

// rtsp_instance_t *rtsp_create(uint16_t port, rtsp_request_callback request_cb, void *user_data)
// {
//     if (!request_cb)
//         return NULL;

//     rtsp_instance_internal_t *instance = (rtsp_instance_internal_t *)malloc(sizeof(rtsp_instance_internal_t));
//     if (!instance)
//         return NULL;

//     memset(instance, 0, sizeof(rtsp_instance_internal_t));
//     instance->request_cb = request_cb;
//     instance->user_data = user_data;
//     instance->session_id_counter = 1;

//     // Create TCP server
//     instance->tcp_server = tcp_create_server(port, rtsp_tcp_accept_callback, instance);
//     if (!instance->tcp_server)
//     {
//         free(instance);
//         return NULL;
//     }

//     tcp_set_callbacks(instance->tcp_server, rtsp_tcp_receive_callback, rtsp_tcp_disconnect_callback);

//     printf("[RTSP] Server created on port %u\n", port);
//     return (rtsp_instance_t *)instance;
// }

// int rtsp_poll(rtsp_instance_t *instance, int block_ms)
// {
//     if (!instance)
//         return -1;

//     rtsp_instance_internal_t *i = (rtsp_instance_internal_t *)instance;

//     return tcp_server_poll(i->tcp_server, block_ms);
// }

// void rtsp_close(rtsp_instance_t *instance)
// {
//     if (!instance)
//         return;

//     rtsp_instance_internal_t *i = (rtsp_instance_internal_t *)instance;
//     if (i->tcp_server)
//     {
//         tcp_close_server(i->tcp_server);
//     }
//     free(instance);
// }

// int rtsp_send_response_with_headers(rtsp_instance_t *instance, rtsp_session_t *session,
//                                     uint16_t status, const char *status_text,
//                                     const char *extra_headers,
//                                     const char *content_type, const uint8_t *body, size_t body_len)
// {
//     if (!instance || !session || !status_text)
//         return 0;

//     rtsp_session_internal_t *s = (rtsp_session_internal_t *)session;

//     // Build response
//     char response_header[1024];
//     int header_len = snprintf(response_header, sizeof(response_header),
//                               "RTSP/1.0 %u %s\r\n"
//                               "Server: AirTunes/130.14\r\n"
//                               "CSeq: %u\r\n",
//                               status, status_text, s->last_cseq);

//     // Only add Session header if session_id is set
//     if (s->session_id[0] != '\0')
//     {
//         header_len += snprintf(response_header + header_len, sizeof(response_header) - header_len,
//                                "Session: %s\r\n", s->session_id);
//     }

//     if (extra_headers && extra_headers[0])
//     {
//         header_len += snprintf(response_header + header_len, sizeof(response_header) - header_len,
//                                "%s", extra_headers);
//         if (header_len >= 2 && strncmp(response_header + header_len - 2, "\r\n", 2) != 0)
//             header_len += snprintf(response_header + header_len, sizeof(response_header) - header_len, "\r\n");
//     }

//     if (content_type && body_len > 0)
//     {
//         header_len += snprintf(response_header + header_len, sizeof(response_header) - header_len,
//                                "Content-Type: %s\r\n"
//                                "Content-Length: %zu\r\n",
//                                content_type, body_len);
//     }

//     header_len += snprintf(response_header + header_len, sizeof(response_header) - header_len, "\r\n");

//     static int response_count = 0;
//     response_count++;
//     printf("[RTSP] Sending response #%d (status=%u, cseq=%u, len=%d)\n",
//            response_count, status, s->last_cseq, header_len);
//     if (response_count <= 10)
//     {
//         printf("[RTSP] Response detail #%d:\n%.*s\n",
//                response_count,
//                (header_len < 300 ? header_len : 300), response_header);

//         // // Print hex dump of response (first 200 bytes)
//         // printf("[RTSP] Response hex dump (first %d bytes):\n", header_len < 200 ? header_len : 200);
//         // int hex_len = header_len < 200 ? header_len : 200;
//         // for (int i = 0; i < hex_len; i++)
//         // {
//         //     printf("%02X ", (unsigned char)response_header[i]);
//         //     if ((i + 1) % 16 == 0)
//         //         printf("\n");
//         // }
//         // if (hex_len % 16 != 0)
//         //     printf("\n");
//     }
//     fflush(stdout);

//     // Send header
//     int sent = tcp_send(s->client, (const uint8_t *)response_header, header_len);
//     if (sent <= 0)
//     {
//         printf("[RTSP] WARNING: Failed to send response header (sent=%d)\n", sent);
//         return 0;
//     }
//     if (sent < header_len)
//     {
//         printf("[RTSP] WARNING: Partial send - sent %d of %d bytes\n", sent, header_len);
//     }

//     // Send body if present
//     if (body && body_len > 0)
//     {
//         sent += tcp_send(s->client, body, body_len);
//     }

//     return sent;
// }

// int rtsp_send_response(rtsp_instance_t *instance, rtsp_session_t *session,
//                        uint16_t status, const char *status_text,
//                        const char *content_type, const uint8_t *body, size_t body_len)
// {
//     return rtsp_send_response_with_headers(instance, session, status, status_text,
//                                            NULL, content_type, body, body_len);
// }

// char *rtsp_create_session_id(rtsp_instance_t *instance)
// {
//     if (!instance)
//         return NULL;

//     char *id = (char *)malloc(RTSP_SESSION_ID_LEN);
//     if (!id)
//         return NULL;

//     rtsp_instance_internal_t *i = (rtsp_instance_internal_t *)instance;
//     snprintf(id, RTSP_SESSION_ID_LEN, "%08X", i->session_id_counter++);
//     return id;
// }

// /**
//  * @brief Create a session for a client and return the session object
//  * This should be called when handling SETUP request
//  */
// rtsp_session_t *rtsp_create_session_for_client(rtsp_instance_t *instance, tcp_client_t *client)
// {
//     if (!instance || !client)
//         return NULL;

//     rtsp_instance_internal_t *i = (rtsp_instance_internal_t *)instance;

//     // Check if session already exists for this client
//     for (int idx = 0; idx < i->session_count; idx++)
//     {
//         if (i->sessions[idx].client == client)
//         {
//             // Session already exists, return it
//             return (rtsp_session_t *)&i->sessions[idx];
//         }
//     }

//     // Create new session
//     if (i->session_count >= MAX_RTSP_SESSIONS)
//         return NULL;

//     rtsp_session_internal_t *session = &i->sessions[i->session_count];
//     session->client = client;
//     session->created_at = time(NULL);
//     snprintf(session->session_id, sizeof(session->session_id), "%08X", i->session_id_counter++);
//     i->session_count++;

//     printf("[RTSP] Created session %s for client\n", session->session_id);
//     fflush(stdout);

//     return (rtsp_session_t *)session;
// }

// /**
//  * @brief Get the TCP client from a session
//  */
// tcp_client_t *rtsp_get_session_client(rtsp_session_t *session)
// {
//     if (!session)
//         return NULL;

//     rtsp_session_internal_t *s = (rtsp_session_internal_t *)session;
//     return s->client;
// }

// rtsp_session_t *rtsp_get_session(rtsp_instance_t *instance, tcp_client_t *client)
// {
//     if (!instance || !client)
//         return NULL;

//     rtsp_instance_internal_t *i = (rtsp_instance_internal_t *)instance;
//     for (int idx = 0; idx < i->session_count; idx++)
//     {
//         if (i->sessions[idx].client == client)
//         {
//             return (rtsp_session_t *)&i->sessions[idx];
//         }
//     }
//     return NULL;
// }

// void rtsp_session_set_user_data(rtsp_session_t *session, void *user_data)
// {
//     if (!session)
//         return;

//     rtsp_session_internal_t *s = (rtsp_session_internal_t *)session;
//     s->user_data = user_data;
// }

// void *rtsp_session_get_user_data(rtsp_session_t *session)
// {
//     if (!session)
//         return NULL;

//     rtsp_session_internal_t *s = (rtsp_session_internal_t *)session;
//     return s->user_data;
// }
