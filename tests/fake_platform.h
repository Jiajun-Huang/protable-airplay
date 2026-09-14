#ifndef TEST_FAKE_PLATFORM_H
#define TEST_FAKE_PLATFORM_H

#include "service/server.h"

/* Deterministic platform double shared by service and lifecycle tests. */

/* Clear captured calls and optionally fail the requested platform operation. */
void fake_reset(int fail_step);

/* Captured resource counts, call counts, and injected I/O status. */
extern int fake_resources, fake_mutexes, fake_steps, fake_io_error;
extern int fake_audio_writes, fake_samples;

/* Most recent short PCM write captured by the fake audio backend. */
extern int16_t fake_pcm[4];

/* Optional server stopped by a fake platform callback during a test. */
extern airplay_server_t *fake_stop_server;

#endif
