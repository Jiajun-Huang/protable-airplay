#include "udp_if.h"
#include "mdns.h"
#include "network_util.h"
#include "rtsp.h"
#include "rtp.h"
#include "audio_pipeline.h"
#include "audio_output.h"
#include "airplay/airplay_discovery.h"
#include "airplay/airplay_rtsp.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_stop_discovery = 0;
static volatile LONG g_stop_rtp = 0;

static audio_pipeline_t g_audio_pipeline;
static audio_output_device_t *g_audio_output = NULL;

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

static void audio_pipeline_on_audio_data(const int16_t *samples, size_t sample_count, void *user_data)
{
    (void)user_data;

    if (!g_audio_output || !samples || sample_count == 0)
        return;

    // Write decoded PCM samples to active output backend.
    int written = audio_output_write(g_audio_output, samples, sample_count);
    if (written < 0)
    {
        static int logged_once = 0;
        if (!logged_once++)
            printf("[audio] Error writing audio samples\n");
    }
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

            if (rec)
            {
                sdp_session_t session;
                if (airplay_rtsp_get_announced_session(&session) != 0)
                {
                    printf("[audio] No ANNOUNCE SDP yet; waiting before playback\n");
                    Sleep(10);
                    continue;
                }

                if (audio_pipeline_configure(&g_audio_pipeline, &session) != 0)
                {
                    printf("[audio] Failed to configure pipeline from SDP\n");
                    Sleep(10);
                    continue;
                }

                if (!g_audio_output)
                {
                    g_audio_output = audio_output_create(session.sample_rate,
                                                         (uint8_t)session.channels,
                                                         (uint8_t)session.bits_per_sample);
                    if (g_audio_output)
                    {
                        printf("[audio] Output created from SDP (%uHz, %u-ch, %u-bit)\n",
                               session.sample_rate, session.channels, session.bits_per_sample);
                        if (audio_pipeline_start(&g_audio_pipeline) != 0)
                            printf("[audio] Failed to start audio pipeline\n");
                    }
                    else
                    {
                        printf("[audio] Failed to create output device\n");
                    }
                }
            }
            else
            {
                // Stop audio playback and close output.
                if (g_audio_output)
                {
                    audio_pipeline_stop(&g_audio_pipeline);
                    audio_output_close(g_audio_output);
                    g_audio_output = NULL;
                    airplay_rtsp_clear_announced_session();
                    printf("[audio] Output closed\n");
                }
            }
        }

        // Poll audio pipeline to process RTP packets
        if (rec && audio_pipeline_get_state(&g_audio_pipeline) == AUDIO_PIPELINE_PLAYING)
        {
            audio_pipeline_poll(&g_audio_pipeline, 20);
        }

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
        // Create audio pipeline (includes its own RTP receiver).
        audio_pipeline_config_t audio_cfg;
        memset(&audio_cfg, 0, sizeof(audio_cfg));
        audio_cfg.audio_port = 6000;
        audio_cfg.control_port = 6001;
        audio_cfg.timing_port = 6002;
        audio_cfg.on_audio_data = audio_pipeline_on_audio_data;
        audio_cfg.on_state_change = NULL;
        audio_cfg.user_data = NULL;

        if (audio_pipeline_create(&g_audio_pipeline, &audio_cfg) != 0)
        {
            printf("[main] Failed to create audio pipeline\n");
            InterlockedExchange((LONG *)&g_stop_discovery, 1);
            WaitForSingleObject(discovery_thread, INFINITE);
            CloseHandle(discovery_thread);
            airplay_discovery_deinit();
            return -1;
        }

        rtp_thread = CreateThread(NULL, 0, rtp_thread_proc, NULL, 0, NULL);
        if (!rtp_thread)
        {
            printf("[main] Failed to create RTP thread\n");
            audio_pipeline_close(&g_audio_pipeline);
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

    // Cleanup on RTSP server exit
    if (g_audio_output)
    {
        audio_output_close(g_audio_output);
        g_audio_output = NULL;
    }

    audio_pipeline_close(&g_audio_pipeline);

    InterlockedExchange((LONG *)&g_stop_rtp, 1);
    WaitForSingleObject(rtp_thread, INFINITE);
    CloseHandle(rtp_thread);

    InterlockedExchange((LONG *)&g_stop_discovery, 1);
    WaitForSingleObject(discovery_thread, INFINITE);
    CloseHandle(discovery_thread);
    airplay_discovery_deinit();

    printf("[main] RTSP server exited with code %d\n", result);
    return result;
}
