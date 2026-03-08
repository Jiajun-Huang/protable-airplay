// #ifndef AIRPLAY_RTSP_H
// #define AIRPLAY_RTSP_H

// #include <stdint.h>
// #include <stddef.h>
// #include "rtsp.h"
// #include "audio_pipeline.h"

// // Transparent structure for user-managed memory allocation
// typedef struct
// {
//     rtsp_instance_t *rtsp;
//     audio_pipeline_t *pipeline;
//     uint16_t server_port;
//     uint16_t control_port;
//     uint16_t timing_port;
//     void (*on_announce_sdp)(const uint8_t *sdp, size_t sdp_len, void *user_data);
//     void (*on_setup_transport)(const char *transport, void *user_data);
//     void (*on_record)(uint16_t seq, uint32_t rtptime, void *user_data);
//     void (*on_flush)(void *user_data);
//     void (*on_teardown)(void *user_data);
//     void (*on_set_volume_db)(float volume_db, void *user_data);
//     void (*on_set_progress)(uint32_t start, uint32_t current, uint32_t end, void *user_data);
//     void *user_data;
// } airplay_rtsp_t;

// typedef struct
// {
//     uint16_t server_port;
//     uint16_t control_port;
//     uint16_t timing_port;
// } airplay_rtsp_config_t;

// typedef struct
// {
//     void (*on_announce_sdp)(const uint8_t *sdp, size_t sdp_len, void *user_data);
//     void (*on_setup_transport)(const char *transport, void *user_data);
//     void (*on_record)(uint16_t seq, uint32_t rtptime, void *user_data);
//     void (*on_flush)(void *user_data);
//     void (*on_teardown)(void *user_data);
//     void (*on_set_volume_db)(float volume_db, void *user_data);
//     void (*on_set_progress)(uint32_t start, uint32_t current, uint32_t end, void *user_data);
// } airplay_rtsp_callbacks_t;

// /**
//  * @brief Create and initialize AirPlay RTSP server
//  * Memory for the server struct must be allocated by the caller.
//  *
//  * @param server pre-allocated RTSP server struct (user-managed memory)
//  * @param port RTSP server port
//  * @param config RTSP configuration
//  * @param callbacks RTSP event callbacks
//  * @param user_data user context pointer
//  * @return 0 on success, -1 on error
//  */
// int airplay_rtsp_create(airplay_rtsp_t *server,
//                         uint16_t port,
//                         const airplay_rtsp_config_t *config,
//                         const airplay_rtsp_callbacks_t *callbacks,
//                         void *user_data);

// int airplay_rtsp_poll(airplay_rtsp_t *server, int block_ms);

// void airplay_rtsp_close(airplay_rtsp_t *server);

// #endif // AIRPLAY_RTSP_H
