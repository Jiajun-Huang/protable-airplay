#include "udp_if.h"
#include "mdns.h"
#include "network_util.h"
#include "rtsp.h"
#include "rtp.h"
#include "airplay/airplay_discovery.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_stop_discovery = 0;
static volatile LONG g_stop_rtp = 0;

static rtp_receiver_t g_rtp_receiver;
static int g_rtp_created = 0;

static int discovery_should_stop(void)
{
    return (InterlockedCompareExchange((LONG *)&g_stop_discovery, 0, 0) != 0) ? 1 : 0;
}

static DWORD WINAPI discovery_thread_proc(LPVOID param)
{
    (void)param;
    airplay_discovery_run(discovery_should_stop);
    return 0;
}

static void rtp_audio_cb(const rtp_packet_t *packet, void *user_data)
{
    (void)user_data;
    (void)packet;
    /* Raw RTP audio payload bytes are logged in rtp_receiver_poll. */
}

static void rtp_control_cb(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    (void)data;
    printf("[rtp] control packet: %zu bytes\n", len);
}

static void rtp_timing_cb(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    (void)data;
    printf("[rtp] timing packet: %zu bytes\n", len);
}

static DWORD WINAPI rtp_thread_proc(LPVOID param)
{
    int last_record = -1;
    (void)param;

    while (InterlockedCompareExchange((LONG *)&g_stop_rtp, 0, 0) == 0)
    {
        int rec = rtsp_is_recording();
        if (rec != last_record)
        {
            printf("[rtp] stream state: %s\n", rec ? "RECORDING" : "IDLE");
            last_record = rec;
        }

        rtp_receiver_poll(&g_rtp_receiver, rec ? 50 : 10);
        Sleep(5);
    }

    return 0;
}

int main(void)
{
    int result;
    HANDLE discovery_thread;
    HANDLE rtp_thread;
    rtsp_instance_t rtsp_server;
    const char *deviceid_txt = "deviceid=1C:CE:51:6D:2E:30";

    const char *raop_txt[] = {
        deviceid_txt,
        "txtvers=1",
        "ch=2",
        "cn=0,1",
        "da=true",
        "et=0,1",
        "md=0,1,2",
        "pw=false",
        "sv=false",
        "sr=44100",
        "ss=16",
        "tp=TCP,UDP",
        "vn=65537",
        "vs=130.14",
        "am=PortableSpeaker",
        "ek=0",
        "sf=0x4"};

    const char *airplay_txt[] = {
        deviceid_txt,
        "txtvers=1",
        "ch=2",
        "cn=0,1",
        "da=true",
        "et=0,1",
        "md=0,1,2",
        "pw=false",
        "sv=false",
        "sr=44100",
        "ss=16",
        "tp=TCP,UDP",
        "vn=65537",
        "vs=130.14",
        "am=PortableSpeaker",
        "sf=0x4"};

    printf("[main] Initializing AirPlay discovery (IP=10.0.0.178, MAC=1CCE516D2E30)...\n");
    fflush(stdout);

    result = airplay_discovery_init("TestSpeaker", "1CCE516D2E30", "10.0.0.178",
                                    raop_txt, sizeof(raop_txt) / sizeof(raop_txt[0]),
                                    airplay_txt, sizeof(airplay_txt) / sizeof(airplay_txt[0]));
    if (result != 0)
    {
        printf("airplay_discovery_init failed: %d\n", result);
        return -1;
    }

    printf("[main] Discovery initialized successfully. Starting mDNS loop...\n");
    fflush(stdout);

    discovery_thread = CreateThread(NULL, 0, discovery_thread_proc, NULL, 0, NULL);
    if (!discovery_thread)
    {
        printf("[main] Failed to create discovery thread\n");
        airplay_discovery_deinit();
        return -1;
    }

    printf("[main] Discovery thread started. Starting RTSP server on main thread...\n");
    fflush(stdout);

    {
        rtp_receiver_config_t rtp_cfg;
        memset(&rtp_cfg, 0, sizeof(rtp_cfg));
        rtp_cfg.audio_port = 6000;
        rtp_cfg.control_port = 6001;
        rtp_cfg.timing_port = 6002;
        rtp_cfg.audio_cb = rtp_audio_cb;
        rtp_cfg.control_cb = rtp_control_cb;
        rtp_cfg.timing_cb = rtp_timing_cb;
        rtp_cfg.user_data = NULL;

        if (rtp_receiver_create(&g_rtp_receiver, &rtp_cfg) != 0)
        {
            printf("[main] Failed to create RTP receiver\n");
            InterlockedExchange((LONG *)&g_stop_discovery, 1);
            WaitForSingleObject(discovery_thread, INFINITE);
            CloseHandle(discovery_thread);
            airplay_discovery_deinit();
            return -1;
        }
        g_rtp_created = 1;

        rtp_thread = CreateThread(NULL, 0, rtp_thread_proc, NULL, 0, NULL);
        if (!rtp_thread)
        {
            printf("[main] Failed to create RTP thread\n");
            rtp_receiver_close(&g_rtp_receiver);
            g_rtp_created = 0;
            InterlockedExchange((LONG *)&g_stop_discovery, 1);
            WaitForSingleObject(discovery_thread, INFINITE);
            CloseHandle(discovery_thread);
            airplay_discovery_deinit();
            return -1;
        }
    }

    result = rtsp_server_create(&rtsp_server, 5000);
    if (result != 0)
    {
        printf("[main] rtsp_server_create failed: %d\n", result);
        InterlockedExchange((LONG *)&g_stop_discovery, 1);
        WaitForSingleObject(discovery_thread, INFINITE);
        CloseHandle(discovery_thread);
        airplay_discovery_deinit();
        return -1;
    }

    result = rtsp_server_start(&rtsp_server);

    InterlockedExchange((LONG *)&g_stop_rtp, 1);
    WaitForSingleObject(rtp_thread, INFINITE);
    CloseHandle(rtp_thread);
    if (g_rtp_created)
    {
        rtp_receiver_close(&g_rtp_receiver);
        g_rtp_created = 0;
    }

    InterlockedExchange((LONG *)&g_stop_discovery, 1);
    WaitForSingleObject(discovery_thread, INFINITE);
    CloseHandle(discovery_thread);
    airplay_discovery_deinit();

    printf("[main] RTSP server exited with code %d\n", result);
    return result;
}
