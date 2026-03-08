#include "../../include/platform_if.h"
#include <stdint.h>
#include <stddef.h>

// TODO: I2S DMA stream implementation

void i2s_play_pcm(int16_t *samples, size_t frames)
{
    (void)samples;
    (void)frames;
}

const raop_audio_if_t i2s_audio_if = {
    .play_pcm = i2s_play_pcm,
};
