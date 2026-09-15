#include "fake_platform.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            LOG_ERROR( "%s:%d: %s\n", __FILE__, __LINE__, #x);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static airplay_server_t server;
static const airplay_config_t config = {"127.0.0.1", "001122334455", "TestSpeaker"};

int main(void)
{
    fake_reset(0);
    CHECK(airplay_server_init(&server, &config) == 0);
    int init_steps = fake_steps;
    CHECK(fake_resources == 6 && fake_mutexes == 2);
    airplay_server_stop(&server);
    airplay_mdns_main(&server);
    airplay_rtsp_main(&server);
    airplay_audio_main(&server);
    CHECK(airplay_server_result(&server) == 0);
    airplay_server_deinit(&server);
    airplay_server_deinit(&server);
    CHECK(fake_resources == 0 && fake_mutexes == 0);
    for (int step = 1; step <= init_steps; ++step)
    {
        fake_reset(step);
        CHECK(airplay_server_init(&server, &config) != 0);
        CHECK(fake_resources == 0 && fake_mutexes == 0);
    }
    void (*entries[])(void *) = {airplay_mdns_main, airplay_rtsp_main, airplay_audio_main};
    for (size_t i = 0; i < 3; ++i)
    {
        fake_reset(0);
        CHECK(airplay_server_init(&server, &config) == 0);
        fake_io_error = 1;
        entries[i](&server);
        CHECK(airplay_server_is_stopping(&server) && airplay_server_result(&server) == -1);
        airplay_server_deinit(&server);
        CHECK(fake_resources == 0 && fake_mutexes == 0);
    }
    fake_reset(0);
    CHECK(airplay_server_init(&server, &config) == 0);
    os_mutex_lock(&server.rtsp.state_lock);
    server.rtsp.stream.has_session = server.rtsp.stream.recording = 1;
    server.rtsp.stream.generation = 1;
    server.rtsp.stream.session = (sdp_session_t){.codec = SDP_CODEC_PCM,
                                                 .sample_rate = 44100,
                                                 .channels = 2,
                                                 .bits_per_sample = 16,
                                                 .frames_per_packet = 2,
                                                 .payload_type = 96};
    server.rtsp.stream.volume_db = -6.0206f;
    os_mutex_unlock(&server.rtsp.state_lock);
    fake_stop_server = &server;
    airplay_audio_main(&server);
    CHECK(fake_audio_writes == 1 && fake_samples == 4);
    CHECK(fake_pcm[0] >= 499 && fake_pcm[0] <= 500);
    CHECK(fake_pcm[1] >= -500 && fake_pcm[1] <= -499);
    CHECK(fake_pcm[2] >= 16382 && fake_pcm[2] <= 16384);
    CHECK(fake_pcm[3] >= -16385 && fake_pcm[3] <= -16383);
    CHECK(airplay_server_result(&server) == 0 && server.audio == NULL);
    airplay_server_deinit(&server);
    CHECK(fake_resources == 0 && fake_mutexes == 0);
    LOG_INFO(
         "Service lifecycle and PCM checks passed; context bytes=%zu\n", sizeof(server));
    return 0;
}
