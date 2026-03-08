// #include "airplay_rtsp.h"
// #include "sdp.h"
// #include "audio_pipeline.h"
// #include "crypto_utils.h"
// #include "../platform/windows/audio.h"
// #include "../platform/tcp_if.h"

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>

// // Internal RTSP session structure (from rtsp.c) - needed to access last_cseq
// #define RTSP_SESSION_ID_LEN 16
// typedef struct
// {
//     tcp_client_t *client;
//     char session_id[RTSP_SESSION_ID_LEN];
//     uint32_t last_cseq;
//     time_t created_at;
//     void *user_data;
// } rtsp_session_internal_t;

// // No longer needed - structure is now defined in header for user allocation
// // typedef struct airplay_rtsp airplay_rtsp_internal_t;

// static const char *airplay_rtsp_get_path(const char *uri)
// {
//     if (!uri)
//         return "/";

//     if (strncmp(uri, "rtsp://", 7) == 0)
//     {
//         const char *after_scheme = uri + 7;
//         const char *slash = strchr(after_scheme, '/');
//         return slash ? slash : "/";
//     }

//     return uri;
// }

// static void airplay_rtsp_parse_rtp_info(const char *rtp_info, uint16_t *seq, uint32_t *rtptime)
// {
//     if (!rtp_info)
//         return;

//     const char *seq_pos = strstr(rtp_info, "seq=");
//     const char *rtp_pos = strstr(rtp_info, "rtptime=");

//     if (seq && seq_pos)
//         *seq = (uint16_t)atoi(seq_pos + 4);
//     if (rtptime && rtp_pos)
//         *rtptime = (uint32_t)strtoul(rtp_pos + 8, NULL, 10);
// }

// static void airplay_rtsp_parse_text_parameters(const rtsp_request_t *request,
//                                                const airplay_rtsp_t *server,
//                                                void *user_data)
// {
//     if (!request || !server || !request->body || request->body_len == 0)
//         return;

//     if (strcasecmp(request->content_type, "text/parameters") != 0)
//         return;

//     char body[1024];
//     size_t copy_len = request->body_len;
//     if (copy_len >= sizeof(body))
//         copy_len = sizeof(body) - 1;

//     memcpy(body, request->body, copy_len);
//     body[copy_len] = '\0';

//     char *volume_pos = strstr(body, "volume:");
//     if (volume_pos && server->on_set_volume_db)
//     {
//         float volume_db = (float)atof(volume_pos + 7);
//         server->on_set_volume_db(volume_db, user_data);
//     }

//     char *progress_pos = strstr(body, "progress:");
//     if (progress_pos && server->on_set_progress)
//     {
//         uint32_t start = 0;
//         uint32_t current = 0;
//         uint32_t end = 0;
//         if (sscanf_s(progress_pos + 9, "%u/%u/%u", &start, &current, &end) == 3)
//             server->on_set_progress(start, current, end, user_data);
//     }
// }

// static void airplay_rtsp_handle_request(rtsp_instance_t *instance,
//                                         rtsp_session_t *session,
//                                         const rtsp_request_t *request,
//                                         void *user_data)
// {
//     airplay_rtsp_t *server = (airplay_rtsp_t *)user_data;
//     if (!server || !request)
//         return;

//     const char *path = airplay_rtsp_get_path(request->uri);

//     printf("[airplay_rtsp] Received %s request on path: %s (CSeq: %u)\n",
//            request->method == RTSP_METHOD_ANNOUNCE ? "ANNOUNCE" : request->method == RTSP_METHOD_SETUP       ? "SETUP"
//                                                               : request->method == RTSP_METHOD_RECORD        ? "RECORD"
//                                                               : request->method == RTSP_METHOD_FLUSH         ? "FLUSH"
//                                                               : request->method == RTSP_METHOD_TEARDOWN      ? "TEARDOWN"
//                                                               : request->method == RTSP_METHOD_OPTIONS       ? "OPTIONS"
//                                                               : request->method == RTSP_METHOD_GET_PARAMETER ? "GET_PARAMETER"
//                                                               : request->method == RTSP_METHOD_SET_PARAMETER ? "SET_PARAMETER"
//                                                                                                              : "UNKNOWN",
//            path, request->cseq);

//     if (request->user_agent[0] != '\0')
//     {
//         printf("[airplay_rtsp] User-Agent: %s\n", request->user_agent);
//     }

//     fflush(stdout);

//     switch (request->method)
//     {
//     case RTSP_METHOD_OPTIONS:
//     {
//         char response_headers[2048] = {0};
//         strcpy(response_headers, "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET\r\n");

//         // Check if Apple-Challenge is present and generate response
//         if (request->apple_challenge[0] != '\0')
//         {
//             printf("[airplay_rtsp] OPTIONS with Apple-Challenge: %s\n", request->apple_challenge);

//             // Get local IP and MAC address
//             uint8_t local_ip[4];
//             uint8_t mac_addr[6];
//             tcp_client_t *client = rtsp_get_session_client(session);

//             if (tcp_get_local_ip(client, local_ip) == 0 &&
//                 tcp_get_local_mac_for_ip(local_ip, mac_addr) == 0)
//             {
//                 printf("[airplay_rtsp] Signing with IP=%u.%u.%u.%u, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
//                        local_ip[0], local_ip[1], local_ip[2], local_ip[3],
//                        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

//                 char apple_response[384];
//                 if (apple_challenge_response(request->apple_challenge, local_ip, mac_addr, apple_response) == 0)
//                 {
//                     // Add Apple-Response header
//                     strcat(response_headers, "Apple-Response: ");
//                     strcat(response_headers, apple_response);
//                     strcat(response_headers, "\r\n");
//                     printf("[airplay_rtsp] Generated Apple-Response (%zu chars)\n", strlen(apple_response));
//                 }
//                 else
//                 {
//                     printf("[airplay_rtsp] ERROR: Failed to generate Apple-Response\n");
//                 }
//             }
//             else
//             {
//                 printf("[airplay_rtsp] ERROR: Failed to get local IP/MAC\n");
//             }
//         }

//         rtsp_send_response_with_headers(instance, session, 200, "OK", response_headers, NULL, NULL, 0);
//         return;
//     }
//     case RTSP_METHOD_ANNOUNCE:
//     {
//         // Parse SDP and configure audio pipeline
//         if (request->body && request->body_len > 0)
//         {
//             printf("[airplay_rtsp] ANNOUNCE body length: %zu bytes\n", request->body_len);
//             sdp_session_t sdp_session;
//             int sdp_result = sdp_parse(request->body, request->body_len, &sdp_session);
//             printf("[airplay_rtsp] SDP parse result: %d\n", sdp_result);

//             if (sdp_result == 0)
//             {
//                 printf("[airplay_rtsp] SDP parsed: codec=%d, rate=%u, channels=%u\n",
//                        sdp_session.codec, sdp_session.sample_rate, sdp_session.channels);

//                 if (server->pipeline)
//                 {
//                     audio_pipeline_configure(server->pipeline, &sdp_session);

//                     // Initialize WASAPI with the audio format from SDP
//                     printf("[airplay_rtsp] Initializing audio: %u Hz, %u channels\n",
//                            sdp_session.sample_rate, sdp_session.channels);
//                     if (win_audio_init(sdp_session.sample_rate, sdp_session.channels, 16) != 0)
//                     {
//                         fprintf(stderr, "[airplay_rtsp] Warning: Failed to initialize audio output\n");
//                     }
//                 }
//             }
//             else
//             {
//                 printf("[airplay_rtsp] WARNING: SDP parse failed (result=%d)\n", sdp_result);
//             }
//         }

//         if (server->on_announce_sdp)
//             server->on_announce_sdp(request->body, request->body_len, server->user_data);
//         rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//         return;
//     }

//     case RTSP_METHOD_SETUP:
//     {
//         printf("[airplay_rtsp] SETUP request - client transport: %s\n",
//                request->transport ? request->transport : "NOT PROVIDED");

//         // Create/get session for this client (this persists the session)
//         tcp_client_t *client = rtsp_get_session_client(session);
//         uint32_t current_cseq = request->cseq; // Save CSeq before session switch
//         rtsp_session_t *real_session = rtsp_create_session_for_client(instance, client);
//         if (real_session)
//         {
//             session = real_session; // Use the real persistent session
//             // Update the CSeq in the new session (it was initialized to 0)
//             rtsp_session_internal_t *internal = (rtsp_session_internal_t *)session;
//             internal->last_cseq = current_cseq;
//         }

//         if (server->on_setup_transport)
//             server->on_setup_transport(request->transport, server->user_data);

//         char headers[512];
//         snprintf(headers, sizeof(headers),
//                  "Transport: RTP/AVP/UDP;unicast;mode=record;server_port=%u;control_port=%u;timing_port=%u\r\n"
//                  "Audio-Jack-Status: connected\r\n",
//                  server->server_port,
//                  server->control_port,
//                  server->timing_port);

//         printf("[airplay_rtsp] SETUP response - server transport: RTP/AVP/UDP;unicast;mode=record;server_port=%u;control_port=%u;timing_port=%u\n",
//                server->server_port, server->control_port, server->timing_port);

//         rtsp_send_response_with_headers(instance, session, 200, "OK", headers, NULL, NULL, 0);
//         return;
//     }

//     case RTSP_METHOD_RECORD:
//     {
//         uint16_t seq = 0;
//         uint32_t rtptime = 0;
//         airplay_rtsp_parse_rtp_info(request->rtp_info, &seq, &rtptime);

//         printf("[airplay_rtsp] RECORD received: seq=%u, rtptime=%u\n", seq, rtptime);
//         printf("[airplay_rtsp] Pipeline state before start: %s\n",
//                server->pipeline ? "exists" : "NULL");

//         // Start audio pipeline
//         if (server->pipeline)
//         {
//             int start_result = audio_pipeline_start(server->pipeline);
//             printf("[airplay_rtsp] audio_pipeline_start returned: %d\n", start_result);
//             if (start_result != 0)
//                 fprintf(stderr, "[airplay_rtsp] WARNING: audio_pipeline_start failed\n");
//         }

//         if (server->on_record)
//             server->on_record(seq, rtptime, server->user_data);

//         rtsp_send_response_with_headers(instance, session, 200, "OK",
//                                         "Audio-Latency: 2205\r\n", NULL, NULL, 0);
//         return;
//     }

//     case RTSP_METHOD_FLUSH:
//     case RTSP_METHOD_FLUSHBUFFERED:
//     {
//         // Flush audio pipeline
//         if (server->pipeline)
//             audio_pipeline_flush(server->pipeline);

//         if (server->on_flush)
//             server->on_flush(server->user_data);

//         if (request->rtp_info[0] != '\0')
//         {
//             char headers[320];
//             snprintf(headers, sizeof(headers), "RTP-Info: %s\r\n", request->rtp_info);
//             rtsp_send_response_with_headers(instance, session, 200, "OK", headers, NULL, NULL, 0);
//         }
//         else
//         {
//             rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//         }
//         return;
//     }

//     case RTSP_METHOD_TEARDOWN:
//         // Stop audio pipeline
//         if (server->pipeline)
//             audio_pipeline_stop(server->pipeline);

//         if (server->on_teardown)
//             server->on_teardown(server->user_data);
//         rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//         return;

//     case RTSP_METHOD_SET_PARAMETER:
//         airplay_rtsp_parse_text_parameters(request, server, server->user_data);

//         // Check for volume parameter and set on pipeline
//         if (request->body && request->body_len > 0 &&
//             strcasecmp(request->content_type, "text/parameters") == 0)
//         {
//             char body[1024];
//             size_t copy_len = request->body_len < sizeof(body) - 1 ? request->body_len : sizeof(body) - 1;
//             memcpy(body, request->body, copy_len);
//             body[copy_len] = '\0';

//             char *volume_pos = strstr(body, "volume:");
//             if (volume_pos && server->pipeline)
//             {
//                 float volume_db = (float)atof(volume_pos + 7);
//                 audio_pipeline_set_volume(server->pipeline, volume_db);
//             }
//         }

//         rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//         return;

//     case RTSP_METHOD_GET_PARAMETER:
//     {
//         // Check what parameter is being requested
//         if (request->body && request->body_len > 0)
//         {
//             char param[64] = {0};
//             size_t copy_len = request->body_len < sizeof(param) - 1 ? request->body_len : sizeof(param) - 1;
//             memcpy(param, request->body, copy_len);
//             param[copy_len] = '\0';

//             // Trim whitespace/newlines
//             for (int i = copy_len - 1; i >= 0; i--)
//             {
//                 if (param[i] == '\r' || param[i] == '\n' || param[i] == ' ')
//                     param[i] = '\0';
//                 else
//                     break;
//             }

//             printf("[airplay_rtsp] GET_PARAMETER request for: '%s'\n", param);

//             if (strcmp(param, "volume") == 0)
//             {
//                 // Send current volume (default -20.0 dB)
//                 const char *volume_response = "volume: -20.000000\r\n";
//                 rtsp_send_response(instance, session, 200, "OK",
//                                    "text/parameters",
//                                    (const uint8_t *)volume_response,
//                                    strlen(volume_response));
//                 return;
//             }
//         }

//         // Default: empty response
//         rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//         return;
//     }

//     case RTSP_METHOD_GET:
//         if (strcmp(path, "/info") == 0)
//         {
//             static const uint8_t minimal_bplist[] = {'b', 'p', 'l', 'i', 's', 't', '0', '0'};
//             rtsp_send_response(instance, session, 200, "OK",
//                                "application/x-apple-binary-plist",
//                                minimal_bplist, sizeof(minimal_bplist));
//             return;
//         }
//         rtsp_send_response(instance, session, 404, "Not Found", NULL, NULL, 0);
//         return;

//     case RTSP_METHOD_POST:
//         if ((strcmp(path, "/pair-setup") == 0) ||
//             (strcmp(path, "/pair-verify") == 0) ||
//             (strcmp(path, "/fp-setup") == 0) ||
//             (strcmp(path, "/auth-setup") == 0) ||
//             (strcmp(path, "/command") == 0) ||
//             (strcmp(path, "/feedback") == 0) ||
//             (strcmp(path, "/audioMode") == 0))
//         {
//             rtsp_send_response(instance, session, 200, "OK", NULL, NULL, 0);
//             return;
//         }
//         rtsp_send_response(instance, session, 404, "Not Found", NULL, NULL, 0);
//         return;

//     default:
//         rtsp_send_response(instance, session, 501, "Not Implemented", NULL, NULL, 0);
//         return;
//     }
// }

// // Callback wrapper for audio data from pipeline to WASAPI
// static void audio_pipeline_audio_callback(const int16_t *samples, size_t sample_count, void *user_data)
// {
//     const airplay_rtsp_t *server = (const airplay_rtsp_t *)user_data;
//     if (!samples || sample_count == 0)
//         return;

//     uint16_t channels = 2;
//     if (server && server->pipeline && server->pipeline->session.channels > 0)
//         channels = server->pipeline->session.channels;

//     size_t frames = sample_count / channels;
//     if (frames > 0)
//         win_audio_play_pcm(samples, frames);
// }

// int airplay_rtsp_create(airplay_rtsp_t *server,
//                         uint16_t port,
//                         const airplay_rtsp_config_t *config,
//                         const airplay_rtsp_callbacks_t *callbacks,
//                         void *user_data)
// {
//     if (!server)
//         return -1;

//     // Initialize server struct (user-allocated memory)
//     memset(server, 0, sizeof(*server));

//     server->server_port = config ? config->server_port : 6000;
//     server->control_port = config ? config->control_port : 6001;
//     server->timing_port = config ? config->timing_port : 6002;

//     if (callbacks)
//     {
//         server->on_announce_sdp = callbacks->on_announce_sdp;
//         server->on_setup_transport = callbacks->on_setup_transport;
//         server->on_record = callbacks->on_record;
//         server->on_flush = callbacks->on_flush;
//         server->on_teardown = callbacks->on_teardown;
//         server->on_set_volume_db = callbacks->on_set_volume_db;
//         server->on_set_progress = callbacks->on_set_progress;
//     }

//     server->user_data = user_data;

//     server->rtsp = rtsp_create(port, airplay_rtsp_handle_request, server);
//     if (!server->rtsp)
//     {
//         return -1;
//     }

//     // Create audio pipeline
//     audio_pipeline_config_t pipeline_config = {0};
//     pipeline_config.audio_port = server->server_port;
//     pipeline_config.control_port = server->control_port;
//     pipeline_config.timing_port = server->timing_port;
//     pipeline_config.on_audio_data = audio_pipeline_audio_callback;
//     pipeline_config.user_data = server;

//     // Allocate pipeline structure
//     server->pipeline = (audio_pipeline_t *)calloc(1, sizeof(audio_pipeline_t));
//     if (!server->pipeline)
//     {
//         fprintf(stderr, "[airplay_rtsp] Failed to allocate audio pipeline\n");
//         return -1;
//     }

//     if (audio_pipeline_create(server->pipeline, &pipeline_config) != 0)
//     {
//         fprintf(stderr, "[airplay_rtsp] Warning: Failed to create audio pipeline\n");
//         free(server->pipeline);
//         server->pipeline = NULL;
//     }

//     return 0;
// }

// int airplay_rtsp_poll(airplay_rtsp_t *server, int block_ms)
// {
//     if (!server)
//         return -1;

//     // Poll RTSP control channel
//     rtsp_poll(server->rtsp, block_ms);

//     // Poll audio pipeline for RTP packets
//     if (server->pipeline)
//         audio_pipeline_poll(server->pipeline, 0);

//     return 0;
// }

// void airplay_rtsp_close(airplay_rtsp_t *server)
// {
//     if (!server)
//         return;

//     if (server->pipeline)
//     {
//         audio_pipeline_close(server->pipeline);
//         free(server->pipeline);
//         server->pipeline = NULL;
//     }

//     win_audio_close();
//     if (server->rtsp)
//     {
//         rtsp_close(server->rtsp);
//         server->rtsp = NULL;
//     }
//     // Note: server struct itself is user-managed, not freed here
// }
