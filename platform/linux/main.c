#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "net.h"
#include "os.h"
#include "log.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t interrupted;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static void *mdns_thread(void *server)
{
    airplay_mdns_main(server);
    return NULL;
}

static void *rtsp_thread(void *server)
{
    airplay_rtsp_main(server);
    return NULL;
}

static void *audio_thread(void *server)
{
    airplay_audio_main(server);
    return NULL;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static airplay_server_t server;
    airplay_config_t config = {0};
    void *(*entries[])(void *) = {mdns_thread, rtsp_thread, audio_thread};
    pthread_t threads[3];
    struct sigaction action = {0};
    unsigned started;
    int result = 1;

    if ((argc != 3 && argc != 4) || strlen(argv[1]) >= sizeof(config.local_ip) ||
        strlen(argv[2]) != 12 ||
        (argc == 4 && strlen(argv[3]) >= sizeof(config.device_name)))
    {
        LOG_ERROR("main", "Usage: %s IPv4 MAC_HEX [NAME]\n", argv[0]);
        return 1;
    }
    strcpy(config.local_ip, argv[1]);
    strcpy(config.local_mac_hex, argv[2]);
    strcpy(config.device_name, argc == 4 ? argv[3] : AIRPLAY_DEVICE_NAME);
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0)
    {
        LOG_ERROR("main", "Cannot register stop handlers.\n");
        return 1;
    }
    if (net_init() != 0)
    {
        LOG_ERROR("main", "Cannot initialize networking.\n");
        return 1;
    }
    if (airplay_server_init(&server, &config) != 0)
    {
        LOG_ERROR("main", "Cannot initialize AirPlay server.\n");
        net_deinit();
        return 1;
    }
    LOG_INFO("main", "AirPlay: %s (%s, %s). Press Ctrl+C to stop.\n",
           config.device_name, config.local_ip, config.local_mac_hex);
    for (started = 0; started < 3; ++started)
    {
        if (pthread_create(&threads[started], NULL, entries[started], &server) != 0)
        {
            LOG_ERROR("main", "Cannot start service thread.\n");
            break;
        }
    }
    if (started == 3)
    {
        while (!interrupted && !airplay_server_is_stopping(&server))
            os_sleep_ms(50);
        result = 0;
    }
    airplay_server_stop(&server);
    while (started > 0)
        pthread_join(threads[--started], NULL);
    if (airplay_server_result(&server) != 0)
        result = 1;
    airplay_server_deinit(&server);
    net_deinit();
    return result;
}
