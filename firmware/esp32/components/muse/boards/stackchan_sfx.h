/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * Short reaction sounds, synthesized (pure, host-tested): touch (shy, happy)
 * and motion play (dizzy, whoa, slip, protest, relief). 16 kHz mono.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SFX_SHY,      /* a soft rising-then-falling glide, ~0.3 s */
    SFX_HAPPY,    /* three quick rising notes, ~0.3 s */
    SFX_DIZZY,    /* a wobbly falling "wooo", ~1.1 s (shaken) */
    SFX_WHOA,     /* a rising "whoa", ~0.4 s (picked up) */
    SFX_SLIP,     /* a falling "uh-oh", ~0.35 s (tipped over) */
    SFX_PROTEST,  /* three quick beeps, ~0.45 s (upside down) */
    SFX_RELIEF,   /* a breathy falling "phew", ~0.5 s (right way up) */
    SFX_COUNT,
} sfx_t;

#define SFX_RATE 16000

/* Frames in the whole sound. */
size_t sfx_frames(sfx_t s);
/* Renders frames [from, from + n) of the sound into out; returns how many (0 at the end). */
size_t sfx_render(sfx_t s, size_t from, int16_t *out, size_t n);
