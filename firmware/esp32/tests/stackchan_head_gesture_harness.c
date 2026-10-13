/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Reads Si12T OUTPUT1 samples (hex, one per line) and prints one event per sample. */
#include <stdio.h>

#include "boards/stackchan_head_gesture.h"

int main(void)
{
    static const char *const NAMES[] = { "-", "press", "up", "down", "release" };
    stackchan_head_gesture_t g;
    stackchan_head_gesture_reset(&g);
    unsigned v;
    while (scanf("%x", &v) == 1) {
        printf("%s %d\n", NAMES[stackchan_head_gesture_update(&g, (uint8_t)v)], stackchan_head_position((uint8_t)v));
    }
    return 0;
}
