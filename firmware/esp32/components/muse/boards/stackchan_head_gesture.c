/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The Si12T register sequence and gesture thresholds follow M5Stack's StackChan
 * firmware (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD).
 */

#include "stackchan_head_gesture.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void stackchan_head_gesture_reset(stackchan_head_gesture_t *g)
{
    memset(g, 0, sizeof(*g));
}

void stackchan_head_zones(uint8_t output1, uint8_t zones[3])
{
    for (int i = 0; i < 3; i++) {
        zones[i] = (output1 >> (2 * i)) & 0x03;
    }
}

int stackchan_head_position(uint8_t output1)
{
    uint8_t z[3];
    stackchan_head_zones(output1, z);
    int total = z[0] + z[1] + z[2];
    return total ? (z[2] - z[0]) * 100 / total : 0;
}

static void remember(stackchan_head_gesture_t *g, int pos)
{
    g->hist[g->head] = (int16_t)pos;
    g->head = (uint8_t)((g->head + 1) % STACKCHAN_HEAD_WINDOW);
    if (g->count < STACKCHAN_HEAD_WINDOW) {
        g->count++;
    }
}

/* The largest move from any position in the window to pos: +, - or 0. */
static int window_move(const stackchan_head_gesture_t *g, int pos)
{
    int best = 0;
    for (int i = 0; i < g->count; i++) {
        int d = pos - g->hist[i];
        if (abs(d) > abs(best)) {
            best = d;
        }
    }
    return best;
}

/* Follows the one-way run the position is on: a reversal starts a new run
 * from the position before it. */
static void track_run(stackchan_head_gesture_t *g, int pos)
{
    if (pos == g->last) {
        if (g->run_values <= 1) {
            g->run_tick = g->ticks;   /* still at the run's start */
        }
        return;
    }
    g->moved_tick = g->ticks;
    int dir = pos > g->last ? 1 : -1;
    if (dir != g->run_dir) {
        g->run_dir = (int8_t)dir;
        g->run_from = g->last;
        g->run_tick = (uint16_t)(g->ticks - 1);
        g->run_values = 2;
    } else {
        g->run_values++;
    }
    g->last = (int16_t)pos;
}

/* At the release: did the touch end in a stroke that lifted off? */
static int stroke_at_release(const stackchan_head_gesture_t *g)
{
    int span = g->last - g->run_from;
    if (g->swiped || g->ticks > STACKCHAN_HEAD_STROKE_MAX || g->run_values < STACKCHAN_HEAD_RUN_VALUES ||
        g->ticks - g->run_tick > STACKCHAN_HEAD_WINDOW || g->ticks - g->moved_tick > STACKCHAN_HEAD_RUN_END ||
        abs(span) < STACKCHAN_HEAD_RUN_SPAN) {
        return 0;
    }
    return span > 0 ? 1 : -1;
}

stackchan_head_event_t stackchan_head_gesture_update(stackchan_head_gesture_t *g, uint8_t output1)
{
    if (g->release_next) {
        g->release_next = 0;
        return STACKCHAN_HEAD_RELEASE;
    }
    bool raw = (output1 & 0x3F) != 0;   /* any zone at level 1 or more */
    int pos = stackchan_head_position(output1);
    if (raw == (bool)g->touched) {
        g->streak = 0;
    } else if (++g->streak >= STACKCHAN_HEAD_DEBOUNCE) {
        g->streak = 0;
        g->touched = raw;
        g->pending = 0;
        g->count = g->head = 0;
        if (raw) {
            g->swiped = 0;
            g->landing = STACKCHAN_HEAD_LAND;
            g->start = g->last = (int16_t)pos;
            g->ticks = g->run_tick = g->moved_tick = 0;
            g->run_values = 1;
            g->run_dir = 0;
            g->run_from = (int16_t)pos;
            return STACKCHAN_HEAD_PRESS;
        }
        int stroke = stroke_at_release(g);
        if (stroke) {
            g->release_next = 1;
            return stroke > 0 ? STACKCHAN_HEAD_SWIPE_UP : STACKCHAN_HEAD_SWIPE_DOWN;
        }
        return STACKCHAN_HEAD_RELEASE;   /* a lone jump at the end is a lift-off */
    }
    if (g->touched && raw && g->ticks < UINT16_MAX) {
        g->ticks++;
        track_run(g, pos);
    }
    if (!g->touched || !raw || g->swiped) {
        return STACKCHAN_HEAD_NONE;
    }
    if (g->landing) {
        g->landing--;
        g->start = (int16_t)pos;
        if (!g->landing) {
            remember(g, pos);
        }
        return STACKCHAN_HEAD_NONE;
    }
    if (g->pending) {
        if (++g->held >= STACKCHAN_HEAD_CONFIRM) {
            g->swiped = 1;
            return g->pending > 0 ? STACKCHAN_HEAD_SWIPE_UP : STACKCHAN_HEAD_SWIPE_DOWN;
        }
        return STACKCHAN_HEAD_NONE;
    }
    int move = window_move(g, pos);
    remember(g, pos);
    if (move > STACKCHAN_HEAD_SWIPE || move < -STACKCHAN_HEAD_SWIPE) {
        g->pending = move > 0 ? 1 : -1;
        g->held = 0;
    }
    return STACKCHAN_HEAD_NONE;
}
