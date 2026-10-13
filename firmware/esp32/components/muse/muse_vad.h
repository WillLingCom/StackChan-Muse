/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * Speech start/end detection for hands-free turns (CONFIG_MUSE_HANDS_FREE):
 * frame energy against an adaptive noise floor. Pure C (no IDF) so the host
 * tests drive it. It decides only when a turn starts and ends; what was said
 * is Muse's job. Frames are MUSE_VAD_FRAME samples (20 ms at 16 kHz).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSE_VAD_FRAME        320
#define MUSE_VAD_MARGIN_DB    10.0f   /* speech: this far above the noise floor */
#define MUSE_VAD_MIN_DB       (-60.0f)/* and at least this loud (dBFS); 12 dB over the K151 floor */
#define MUSE_VAD_START_FRAMES 5       /* 100 ms of speech-like frames in a row to start */
#define MUSE_VAD_END_FRAMES   40      /* 800 ms of quiet ends the turn */
#define MUSE_VAD_MIN_SPEECH   20      /* 400 ms of speech frames, or it was a noise: drop it */
#define MUSE_VAD_FLOOR_INIT   (-70.0f)
#define MUSE_VAD_BELOW_PEAK_DB 16.0f  /* in speech, a frame this far under the turn's peak is quiet too */

typedef enum {
    MUSE_VAD_NONE,
    MUSE_VAD_START,     /* speech began (the pre-roll holds the frames before) */
    MUSE_VAD_END,       /* the turn ended: enough speech, then MUSE_VAD_END_FRAMES quiet */
    MUSE_VAD_NOISE,     /* it ended without MUSE_VAD_MIN_SPEECH: not a turn */
} muse_vad_event_t;

typedef struct {
    float floor_db;     /* noise floor, tracked while not in speech */
    bool in_speech;
    int run;            /* speech-like frames in a row (before a start) */
    int quiet;          /* quiet frames in a row (in speech) */
    int speech;         /* speech-like frames this turn */
    float peak_db;      /* the turn's loudest, decaying slowly */
} muse_vad_t;

void muse_vad_init(muse_vad_t *v);
/* Only follows the noise floor (no events): for idle audio between conversations,
 * so a turn starts against the room as it is now (a TV on, say). */
void muse_vad_track(muse_vad_t *v, const int16_t *pcm, size_t n);
/* Back to waiting for a start, keeping the noise floor. */
void muse_vad_reset(muse_vad_t *v);
/* RMS level of a frame in dBFS (-100 for silence). */
float muse_vad_frame_db(const int16_t *pcm, size_t n);
/* Feeds one frame; returns at most one event. */
muse_vad_event_t muse_vad_feed(muse_vad_t *v, const int16_t *pcm, size_t n);
