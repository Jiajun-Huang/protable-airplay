#include "pipeline.h"
#include "mdns.h"
#include "rtsp.h"
#include "rtp.h"
#include "audio_pipeline.h"
#include "audio_if.h"
#include "airplay/airplay_discovery.h"
#include "airplay/airplay_rtsp.h"

#include <windows.h>
#include <stdio.h>
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

    if (audio_output_write(g_audio_output, samples, sample_count) < 0)
    {
        static int logged_once = 0;
        if (!logged_once++)
            printf("[audio] Error writing audio samples\n");
    }
}

static DWORD WINAPI rtp_thread_proc(LPVOID param)
{
    int last_record = -1;
    unsigned int last_volume_version = 0;
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
                        float vol = airplay_rtsp_get_volume_db();
                        audio_output_set_volume_db(g_audio_output, vol);
                        audio_pipeline_set_volume(&g_audio_pipeline, vol);
                        last_volume_version = airplay_rtsp_get_volume_version();

                        if (audio_pipeline_start(&g_audio_pipeline) != 0)
                            printf("[audio] Failed to start audio pipeline\n");
                    }
                }
            }
            else if (g_audio_output)
            {
                audio_pipeline_stop(&g_audio_pipeline);
                audio_output_close(g_audio_output);
                g_audio_output = NULL;
                airplay_rtsp_clear_announced_session();
                printf("[audio] Output closed\n");
            }
        }

        if (rec && audio_pipeline_get_state(&g_audio_pipeline) == AUDIO_PIPELINE_PLAYING)
        {
            unsigned int vv = airplay_rtsp_get_volume_version();
            if (vv != last_volume_version)
            {
                float vol = airplay_rtsp_get_volume_db();
                audio_pipeline_set_volume(&g_audio_pipeline, vol);
                if (g_audio_output)
                    audio_output_set_volume_db(g_audio_output, vol);
                last_volume_version = vv;
            }

            audio_pipeline_poll(&g_audio_pipeline, 20);
        }

        Sleep(5);
    }

    return 0;
}

int pipeline_start(const pipeline_startup_t *startup)
{
    int result;
    HANDLE discovery_thread;
    HANDLE rtp_thread;
    rtsp_instance_t rtsp_server;
    audio_pipeline_config_t audio_cfg;
    char deviceid_txt[48];

    if (!startup)
        return -1;

    snprintf(deviceid_txt, sizeof(deviceid_txt), "deviceid=%s", startup->identity.local_mac_colon);

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

    if (airplay_rtsp_set_identity(startup->identity.local_ip, startup->identity.local_mac_hex) != 0)
        return -1;

    result = airplay_discovery_init(startup->identity.device_name,
                                    startup->identity.local_mac_hex,
                                    startup->identity.local_ip,
                                    raop_txt,
                                    sizeof(raop_txt) / sizeof(raop_txt[0]),
                                    airplay_txt,
                                    sizeof(airplay_txt) / sizeof(airplay_txt[0]));
    if (result != 0)
        return -1;

    discovery_thread = CreateThread(NULL, 0, discovery_thread_proc, NULL, 0, NULL);
    if (!discovery_thread)
    {
        airplay_discovery_deinit();
        return -1;
    }

    memset(&audio_cfg, 0, sizeof(audio_cfg));
    audio_cfg.audio_port = 6000;
    audio_cfg.control_port = 6001;
    audio_cfg.timing_port = 6002;
    audio_cfg.on_audio_data = audio_pipeline_on_audio_data;

    if (audio_pipeline_create(&g_audio_pipeline, &audio_cfg) != 0)
    {
        InterlockedExchange((LONG *)&g_stop_discovery, 1);
        WaitForSingleObject(discovery_thread, INFINITE);
        CloseHandle(discovery_thread);
        airplay_discovery_deinit();
        return -1;
    }

    rtp_thread = CreateThread(NULL, 0, rtp_thread_proc, NULL, 0, NULL);
    if (!rtp_thread)
    {
        audio_pipeline_close(&g_audio_pipeline);
        InterlockedExchange((LONG *)&g_stop_discovery, 1);
        WaitForSingleObject(discovery_thread, INFINITE);
        CloseHandle(discovery_thread);
        airplay_discovery_deinit();
        return -1;
    }

    result = rtsp_server_create(&rtsp_server, 5000);
    if (result != 0)
    {
        InterlockedExchange((LONG *)&g_stop_rtp, 1);
        WaitForSingleObject(rtp_thread, INFINITE);
        CloseHandle(rtp_thread);
        InterlockedExchange((LONG *)&g_stop_discovery, 1);
        WaitForSingleObject(discovery_thread, INFINITE);
        CloseHandle(discovery_thread);
        audio_pipeline_close(&g_audio_pipeline);
        airplay_discovery_deinit();
        return -1;
    }

    result = rtsp_server_start(&rtsp_server);

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

    return result;
}
