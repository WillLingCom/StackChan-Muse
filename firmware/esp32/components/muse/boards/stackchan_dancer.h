/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * F8 dance runtime: the tune plays on the voice task once the current turn is
 * over (never over a reply); the head follows the playhead through
 * stackchan_body_follow(); base lights and the screen tint follow it too.
 * A screen tap, a head touch, a press or stackchan_dancer_stop() ends it
 * (music stops, the head holds then lowers slowly, lights off).
 */

#include <stdbool.h>
#include <stddef.h>

/* NULL if it will start (right away when idle), else why not. */
const char *stackchan_dancer_start(int song);
void stackchan_dancer_stop(void);
bool stackchan_dancer_active(void);
/* "happy: ok", "chill: hand on the head", ... or "none". */
void stackchan_dancer_last(char *out, size_t cap);
