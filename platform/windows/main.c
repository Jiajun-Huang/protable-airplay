#include "udp_if.h"
#include "mdns.h"
#include "network_util.h"
#include "rtsp.h"
#include "airplay/airplay_discovery.h"
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_stop_discovery = 0;

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

int main(void)
{
    int result;
    HANDLE discovery_thread;
    rtsp_instance_t rtsp_server;

    const char *txt[] = {
        "txtvers=1",
        "ch=2",
        "cn=0,1,2,3",
        "da=true",
        "et=0,3,5",
        "md=0,1,2",
        "pw=false",
        "sv=false",
        "sr=44100",
        "ss=16",
        "tp=UDP",
        "vn=65537",
        "vs=130.14",
        "am=TestSpeaker",
        "sf=0x4"};

    printf("[main] Initializing AirPlay discovery (IP=10.0.0.178, MAC=1CCE516D2E30)...\n");
    fflush(stdout);

    result = airplay_discovery_init("TestSpeaker", "1CCE516D2E31", "10.0.0.178",
                                    txt, sizeof(txt) / sizeof(txt[0]),
                                    txt, sizeof(txt) / sizeof(txt[0]));
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

    InterlockedExchange((LONG *)&g_stop_discovery, 1);
    WaitForSingleObject(discovery_thread, INFINITE);
    CloseHandle(discovery_thread);
    airplay_discovery_deinit();

    printf("[main] RTSP server exited with code %d\n", result);
    return result;
}
