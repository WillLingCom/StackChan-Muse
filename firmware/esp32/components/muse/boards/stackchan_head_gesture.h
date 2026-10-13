/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The Si12T register sequence and gesture thresholds follow M5Stack's StackChan
 * firmware (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD).
 */

/*
 * StackChan K151 head touch: gestures from the Si12T's three zones.
 * Pure logic (no IDF), so the host tests can drive it. Follows M5Stack's
 * hal_head_touch.cpp (m5stack/StackChan @1b57655): zone weights -100/0/+100,
 * a swipe once the position moves far enough from where the touch began, one
 * swipe per touch. Changes, from tests on the K151:
 *  - a touch or release counts only after STACKCHAN_HEAD_DEBOUNCE samples agree;
 *  - a resting hand lands on one zone and spreads over the next 50-100 ms,
 *    which moved the position by 50 and read as a swipe with the factory's
 *    threshold of 40. The start position now follows the hand for
 *    STACKCHAN_HEAD_LAND samples after the press, and a swipe needs 60;
 *  - a resting hand still drifted (75 to -50 over 3.3 s) and lifting it
 *    shrank the contact to one side just before the release. So a swipe is a
 *    move of 60 within STACKCHAN_HEAD_WINDOW samples, and it counts only once
 *    the hand has stayed on for STACKCHAN_HEAD_CONFIRM samples after it;
 *  - but a real stroke usually lifts off as it reaches the far side (log:
 *    -100 -75 -50 +50 +100, then release). So a touch shorter than
 *    STACKCHAN_HEAD_STROKE_MAX samples that ends in a quick one-way run
 *    through STACKCHAN_HEAD_RUN_VALUES positions spanning STACKCHAN_HEAD_RUN_SPAN,
 *    still moving within STACKCHAN_HEAD_RUN_END samples of the release (a hand
 *    that spread as it landed and then rested isn't), is a swipe too, reported at its release (the release follows on the next
 *    sample). A rest's lift-off is long and moves 1-2 steps.
 * Zone 2 is at the front of the head, zone 0 at the back (user test).
 */
#pragma once

#include <stdint.h>

#define STACKCHAN_HEAD_DEBOUNCE 2       /* samples (50 ms apart) */
#define STACKCHAN_HEAD_LAND 2           /* samples after the press while the hand settles */
#define STACKCHAN_HEAD_SWIPE 60         /* position units, -100..100 */
#define STACKCHAN_HEAD_WINDOW 12        /* samples: the move must happen within 600 ms */
#define STACKCHAN_HEAD_CONFIRM 3        /* samples still touched after the move */
#define STACKCHAN_HEAD_STROKE_MAX 30    /* samples: a stroke's whole touch, 1.5 s */
#define STACKCHAN_HEAD_RUN_VALUES 3     /* distinct positions in the final run */
#define STACKCHAN_HEAD_RUN_SPAN 90      /* position units the final run covers */
#define STACKCHAN_HEAD_RUN_END 4        /* samples from the run's last move to the release */

typedef enum {
    STACKCHAN_HEAD_NONE,
    STACKCHAN_HEAD_PRESS,
    STACKCHAN_HEAD_SWIPE_UP,     /* towards zone 2: back to front */
    STACKCHAN_HEAD_SWIPE_DOWN,   /* towards zone 0: front to back */
    STACKCHAN_HEAD_RELEASE,
} stackchan_head_event_t;

typedef struct {
    uint8_t touched;     /* debounced */
    uint8_t swiped;      /* this touch already swiped */
    uint8_t streak;      /* raw samples in a row that disagree with touched */
    uint8_t landing;     /* samples left while the hand settles */
    uint8_t count;       /* positions in hist */
    uint8_t head;        /* next slot in hist */
    int8_t pending;      /* a move seen, waiting for CONFIRM: -1 / +1, else 0 */
    uint8_t held;        /* touched samples since the move */
    uint8_t release_next;   /* report the release on the next sample */
    uint8_t run_values;  /* distinct positions in the current one-way run */
    int8_t run_dir;      /* its direction: -1 / +1, 0 before any move */
    uint16_t ticks;      /* touched samples since the press */
    uint16_t run_tick;   /* tick at which the run's first position was last seen */
    uint16_t moved_tick; /* tick of the last position change */
    int16_t run_from;    /* the run's first position */
    int16_t last;        /* the last position seen */
    int16_t start;       /* position when the touch began */
    int16_t hist[STACKCHAN_HEAD_WINDOW];   /* recent positions, oldest at head when full */
} stackchan_head_gesture_t;

void stackchan_head_gesture_reset(stackchan_head_gesture_t *g);
/* Zone intensities (0..3 each) from the Si12T's OUTPUT1 register. */
void stackchan_head_zones(uint8_t output1, uint8_t zones[3]);
/* Weighted position, -100 (zone 0) .. 100 (zone 2); 0 when untouched. */
int stackchan_head_position(uint8_t output1);
/* Feeds one OUTPUT1 sample; returns at most one event. */
stackchan_head_event_t stackchan_head_gesture_update(stackchan_head_gesture_t *g, uint8_t output1);
