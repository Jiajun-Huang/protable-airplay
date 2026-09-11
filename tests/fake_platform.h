#ifndef TEST_FAKE_PLATFORM_H
#define TEST_FAKE_PLATFORM_H
#include "server.h"
void fake_reset(int fail_step);
extern int fake_resources, fake_mutexes, fake_steps, fake_io_error;
extern int fake_audio_writes, fake_samples;
extern int16_t fake_pcm[4];
extern airplay_server_t *fake_stop_server;
#endif
