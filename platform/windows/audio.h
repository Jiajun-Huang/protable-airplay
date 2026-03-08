#ifndef WIN_AUDIO_H
#define WIN_AUDIO_H

#include <stdint.h>
#include <stddef.h>

// Core audio functions
int win_audio_init(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
int win_audio_play_pcm(const int16_t *samples, size_t frames);
int win_audio_set_volume_db(float volume_db);
int win_audio_flush(void);
void win_audio_close(void);

// Callback handlers for RTSP events
void win_audio_on_volume_db(float volume_db, void *user_data);
void win_audio_on_progress(uint32_t start, uint32_t current, uint32_t end, void *user_data);
void win_audio_on_stream_state(const char *state, void *user_data);

#endif // WIN_AUDIO_H
