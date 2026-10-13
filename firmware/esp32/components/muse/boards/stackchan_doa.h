/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * StackChan K151 experimental direction of a near talker (S5, off by
 * default). The two mics sit 3 cm apart below the screen (slot L = the
 * viewer-left hole = the robot's RIGHT). Each 20 ms frame is band-passed to
 * 300-3400 Hz and, if it is speech-loud, cross-correlated over +-3 samples
 * with a parabolic sub-sample peak; a turn's estimate is the median of its
 * first frames. Measured on the K151: front -0.03, robot's left 1 m -0.20,
 * right 1 m +0.17, left 30 cm -0.61 samples (geometric limit +-1.40), so a
 * decision needs a clear margin and otherwise says "unsure". Pure C.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOA_FRAME        320     /* 20 ms at 16 kHz */
#define DOA_MAX_FRAMES   75      /* the first 1.5 s of speech in a turn */
#define DOA_MIN_FRAMES   8
#define DOA_GATE_DB      6.0f    /* speech: this far over the band noise floor */

typedef enum {
    DOA_UNSURE,                  /* too few frames, or too close to the middle */
    DOA_LEFT,                    /* the robot's left */
    DOA_RIGHT,                   /* the robot's right */
    DOA_CENTER,
} doa_side_t;

typedef struct {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
} doa_biquad_t;

typedef struct {
    doa_biquad_t hp[2], lp[2];   /* per channel */
    float floor_db;              /* band noise floor, tracked while not collecting */
    bool collecting;
    int n;                       /* delays collected this turn */
    float delay[DOA_MAX_FRAMES];
    int16_t pend[2 * DOA_FRAME]; /* a partial frame, interleaved */
    size_t pend_n;
} doa_t;

typedef struct {
    doa_side_t side;
    int frames;
    float median, q1, q3;        /* samples; + = the robot's right first */
} doa_result_t;

void doa_init(doa_t *d);
/* Starts a turn's estimate (speech has begun). */
void doa_begin(doa_t *d);
/* Interleaved L/R frames at 16 kHz, any count; always call it (it tracks the floor). */
void doa_feed(doa_t *d, const int16_t *stereo, size_t frames);
/* Ends the turn: the decision with center/threshold in samples. */
doa_result_t doa_end(doa_t *d, float center, float threshold);
/* One frame's delay estimate (exposed for the tests). */
float doa_frame_delay(const float *l, const float *r, size_t n);
