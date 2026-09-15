



/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 * All rights reserved.
 *
 * http://crazney.net/programs/itunes/alac.html
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __ALAC__DECOMP_H
#define __ALAC__DECOMP_H

#include <stddef.h>
#include <stdint.h>

#include "airplay_config.h"

/* Internal low-level ALAC decoder imported by the checked audio adapter. */

typedef struct alac_file alac_file;

/* Allocate a decoder for one sample size and channel count. */
/**
 * @brief alac_create.
 * @param samplesize Parameter named samplesize.
 * @param numchannels Parameter named numchannels.
 * @return Function result.
 */
alac_file *alac_create(int samplesize, int numchannels);
/* Decode one ALAC frame into the caller's PCM byte buffer. */
/**
 * @brief alac_decode_frame.
 * @param alac Parameter named alac.
 * @param inbuffer Parameter named inbuffer.
 * @param inputsize Parameter named inputsize.
 * @param outbuffer Parameter named outbuffer.
 * @param outputsize Parameter named outputsize.
 */
void alac_decode_frame(alac_file *alac,
                       const unsigned char *inbuffer,
                       size_t inputsize,
                       void *outbuffer,
                       int *outputsize);
/* Load the ALAC codec configuration cookie into the decoder. */
/**
 * @brief alac_set_info.
 * @param alac Parameter named alac.
 * @param inputbuffer Parameter named inputbuffer.
 */
void alac_set_info(alac_file *alac, char *inputbuffer);
/* Bind the decoder's working buffers to its embedded fixed storage. */
/**
 * @brief alac_allocate_buffers.
 * @param alac Parameter named alac.
 */
void alac_allocate_buffers(alac_file *alac);
/* Release a decoder returned by alac_create. */
/**
 * @brief alac_free.
 * @param alac Parameter named alac.
 */
void alac_free(alac_file *alac);

struct alac_file
{
    const unsigned char *input_buffer;
    const unsigned char *input_start;
    const unsigned char *input_end;
    int decode_error;
    int input_buffer_bitaccumulator; /* used so we can do arbitrary
                                        bit reads */

    int in_use;

    int samplesize;
    int numchannels;
    int bytespersample;

    /* buffers */
    int32_t *predicterror_buffer_a;
    int32_t *predicterror_buffer_b;

    int32_t *outputsamples_buffer_a;
    int32_t *outputsamples_buffer_b;

    int32_t *uncompressed_bytes_buffer_a;
    int32_t *uncompressed_bytes_buffer_b;

    int32_t predicterror_buffer_a_storage[ALAC_MAX_SAMPLES_PER_FRAME];
    int32_t predicterror_buffer_b_storage[ALAC_MAX_SAMPLES_PER_FRAME];

    int32_t outputsamples_buffer_a_storage[ALAC_MAX_SAMPLES_PER_FRAME];
    int32_t outputsamples_buffer_b_storage[ALAC_MAX_SAMPLES_PER_FRAME];

    int32_t uncompressed_bytes_buffer_a_storage[ALAC_MAX_SAMPLES_PER_FRAME];
    int32_t uncompressed_bytes_buffer_b_storage[ALAC_MAX_SAMPLES_PER_FRAME];

    /* stuff from setinfo */
    uint32_t setinfo_max_samples_per_frame; /* 0x1000 = 4096 */ /* max samples per frame? */
    uint8_t setinfo_7a;                                         /* 0x00 */
    uint8_t setinfo_sample_size;                                /* 0x10 */
    uint8_t setinfo_rice_historymult;                           /* 0x28 */
    uint8_t setinfo_rice_initialhistory;                        /* 0x0a */
    uint8_t setinfo_rice_kmodifier;                             /* 0x0e */
    uint8_t setinfo_7f;                                         /* 0x02 */
    uint16_t setinfo_80;                                        /* 0x00ff */
    uint32_t setinfo_82; /* 0x000020e7 */                       /* max sample size?? */
    uint32_t setinfo_86; /* 0x00069fe4 */                       /* bit rate (avarge)?? */
    uint32_t setinfo_8a_rate;                                   /* 0x0000ac44 */
                                                                /* end setinfo stuff */
};

#endif /* __ALAC__DECOMP_H */
