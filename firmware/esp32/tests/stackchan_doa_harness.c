/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Reads interleaved stereo 16-bit PCM on stdin. argv: begin_frame center threshold.
 * Feeds everything; starts collecting at begin_frame (in 320-sample frames); prints the result. */
#include <stdio.h>
#include <stdlib.h>

#include "boards/stackchan_doa.h"

int main(int argc, char **argv)
{
    int begin = argc > 1 ? atoi(argv[1]) : 0;
    float center = argc > 2 ? (float)atof(argv[2]) : 0.0f, thr = argc > 3 ? (float)atof(argv[3]) : 0.1f;
    static doa_t d;
    doa_init(&d);
    int16_t buf[2 * DOA_FRAME];
    for (int f = 0; fread(buf, sizeof(int16_t), 2 * DOA_FRAME, stdin) == 2 * DOA_FRAME; f++) {
        if (f == begin) {
            doa_begin(&d);
        }
        doa_feed(&d, buf, DOA_FRAME);
    }
    doa_result_t r = doa_end(&d, center, thr);
    static const char *const SIDE[] = { "unsure", "left", "right", "center" };
    printf("%s %d %.3f %.3f %.3f\n", SIDE[r.side], r.frames, r.median, r.q1, r.q3);
    return 0;
}
