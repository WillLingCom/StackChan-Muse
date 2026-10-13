/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Reads raw 16-bit mono PCM (16 kHz) on stdin; prints "<frame> <event>" per event. */
#include <stdio.h>
#include <stdlib.h>

#include "muse_vad.h"

int main(void)
{
    static const char *const NAMES[] = { "none", "start", "end", "noise" };
    muse_vad_t v;
    muse_vad_init(&v);
    int16_t f[MUSE_VAD_FRAME];
    /* VAD_TRACK=N: the first N frames are idle audio (muse_vad_track), the rest a turn. */
    int track = getenv("VAD_TRACK") ? atoi(getenv("VAD_TRACK")) : 0;
    for (int i = 0; fread(f, sizeof(int16_t), MUSE_VAD_FRAME, stdin) == MUSE_VAD_FRAME; i++) {
        if (i < track) {
            muse_vad_track(&v, f, MUSE_VAD_FRAME);
            continue;
        }
        muse_vad_event_t e = muse_vad_feed(&v, f, MUSE_VAD_FRAME);
        if (e != MUSE_VAD_NONE) {
            printf("%d %s\n", i, NAMES[e]);
        }
    }
    return 0;
}
