/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

#include "muse_vad.h"

#include <math.h>

void muse_vad_init(muse_vad_t *v)
{
    v->floor_db = MUSE_VAD_FLOOR_INIT;
    muse_vad_reset(v);
}

void muse_vad_reset(muse_vad_t *v)
{
    v->in_speech = false;
    v->run = v->quiet = v->speech = 0;
    v->peak_db = -100.0f;
}

float muse_vad_frame_db(const int16_t *pcm, size_t n)
{
    if (!n) {
        return -100.0f;
    }
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += (double)pcm[i] * pcm[i];
    }
    double ms = acc / n / (32768.0 * 32768.0);
    return ms > 1e-10 ? (float)(10.0 * log10(ms)) : -100.0f;
}

/* The floor follows quiet quickly and noise slowly; a steady sound well over
 * it (a TV) only very slowly (~10 s), so speech, which comes and goes, barely
 * moves it, but a room that got louder doesn't keep every frame "speech". */
static void follow_floor(muse_vad_t *v, float db, bool loud)
{
    float k = db < v->floor_db ? 0.2f : loud ? 0.004f : 0.02f;
    v->floor_db += (db - v->floor_db) * k;
    if (v->floor_db < -90.0f) {
        v->floor_db = -90.0f;
    }
}

void muse_vad_track(muse_vad_t *v, const int16_t *pcm, size_t n)
{
    if (!v->in_speech) {
        float db = muse_vad_frame_db(pcm, n);
        follow_floor(v, db, db > v->floor_db + MUSE_VAD_MARGIN_DB && db > MUSE_VAD_MIN_DB);
    }
}

muse_vad_event_t muse_vad_feed(muse_vad_t *v, const int16_t *pcm, size_t n)
{
    float db = muse_vad_frame_db(pcm, n);
    bool loud = db > v->floor_db + MUSE_VAD_MARGIN_DB && db > MUSE_VAD_MIN_DB;
    if (!v->in_speech) {
        follow_floor(v, db, loud);
        v->peak_db = loud ? (db > v->peak_db || v->run == 0 ? db : v->peak_db) : -100.0f;
        v->run = loud ? v->run + 1 : 0;
        if (v->run >= MUSE_VAD_START_FRAMES) {
            v->in_speech = true;
            v->speech = v->run;
            v->quiet = 0;
            return MUSE_VAD_START;
        }
        return MUSE_VAD_NONE;
    }
    v->peak_db = db > v->peak_db ? db : v->peak_db - 0.02f;   /* decays 1 dB/s */
    if (loud && db > v->peak_db - MUSE_VAD_BELOW_PEAK_DB) {
        v->speech++;
        v->quiet = 0;
        return MUSE_VAD_NONE;
    }
    if (++v->quiet < MUSE_VAD_END_FRAMES) {
        return MUSE_VAD_NONE;
    }
    muse_vad_event_t ev = v->speech >= MUSE_VAD_MIN_SPEECH ? MUSE_VAD_END : MUSE_VAD_NOISE;
    muse_vad_reset(v);
    return ev;
}
