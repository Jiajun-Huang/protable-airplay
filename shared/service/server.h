#ifndef AIRPLAY_SERVER_H
#define AIRPLAY_SERVER_H

#include "airplay/airplay_discovery.h"
#include "airplay_config.h"
#include "audio.h"
#include "audio/audio_pipeline.h"
#include "os.h"
#include "protocol/rtsp.h"

/* Top-level receiver service. It owns shared protocol state and exposes three
 * blocking service entry points for platform-created threads or tasks. */

/* Runtime identity selected from one network interface. */
typedef struct
{
    char local_ip[16];
    char local_mac_hex[13];
    char device_name[64];
} airplay_config_t;

/* The RAOP instance label also contains a 12-digit MAC and '@' (DNS limit: 63 bytes). */
_Static_assert(sizeof(AIRPLAY_DEVICE_NAME) > 1 && sizeof(AIRPLAY_DEVICE_NAME) <= 51,
               "AIRPLAY_DEVICE_NAME must contain 1..50 UTF-8 bytes");

/* Caller-owned storage: allocate statically on embedded targets, not on task stacks.
 * Treat fields as private; access shared state through the functions below. */
typedef struct
{
    airplay_config_t config;
    os_mutex_t control_lock;
    int stopping;
    int result;
    int initialized;
    int rtsp_initialized;
    int discovery_initialized;
    int audio_initialized;
    rtsp_instance_t rtsp;
    airplay_discovery_t discovery;
    audio_pipeline_t pipeline;
    audio_device_t *audio;
    char deviceid_txt[32];
    const char
        *raop_txt[1 + sizeof((const char *[]){AIRPLAY_RAOP_TXT_ENTRIES}) / sizeof(const char *)];
} airplay_server_t;

/* Platform calls net_init first. All sockets open before init succeeds.
 * Start each of the three service functions exactly once after successful init. */
int airplay_server_init(airplay_server_t *server, const airplay_config_t *config);
/* Run the mDNS discovery loop until the server is stopped. */
void airplay_mdns_main(void *server);
/* Run the RTSP control loop until the server is stopped. */
void airplay_rtsp_main(void *server);
/* Run packet receive, synchronization, decode, and PCM output until stopped. */
void airplay_audio_main(void *server);

/* Thread-safe; service I/O uses finite timeouts so stop does not close live sockets. */
void airplay_server_stop(airplay_server_t *server);
/* Return nonzero after a stop request or fatal service failure. */
int airplay_server_is_stopping(airplay_server_t *server);
/* Return zero for a clean stop or -1 after a service failure. */
int airplay_server_result(airplay_server_t *server);
/* Join every started task, read result, then deinit; platform finally calls net_deinit. */
void airplay_server_deinit(airplay_server_t *server);

#endif
