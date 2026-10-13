/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Limits follow and tighten M5Stack's StackChan
 *  the factory firmware (m5stack/StackChan @1b57655, MIT).
 */

/*
 * StackChan K151 gestures: named moves planned as keyframes in degrees, then
 * checked against the soft limits below. Pure (no IDF) so the host tests can
 * hold the limits to account. The executor (stackchan_body.c) interpolates
 * between keyframes at 50 Hz.
 *
 * Coordinates: yaw degrees from the calibrated forward zero, + = the robot's
 * right (the viewer's left); pitch degrees above the rest the head sits on
 * when the gesture starts (its lower stop), + = up. "left"/"right" in gesture
 * names are the robot's own.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GESTURE_YAW_LIMIT      25.0f    /* deg either side (factory: 128) */
#define GESTURE_PITCH_LIMIT    30.0f    /* deg above rest (factory: 3..87 from zero) */
#define GESTURE_PITCH_LIFT     4.0f     /* off the stop while moving */
#define GESTURE_YAW_SPEED      60.0f    /* deg/s, at speed 1 */
#define GESTURE_PITCH_SPEED    45.0f
#define GESTURE_MAX_MS         5000     /* the whole gesture, back home included */
#define GESTURE_MAX_FRAMES     16
#define GESTURE_MAX_REPEAT     3
#define GESTURE_TURN_DEG       20.0f    /* facing a talker */

typedef enum {
    GESTURE_NOD,
    GESTURE_SHAKE,
    GESTURE_SWAY,        /* "扭扭腰": a slow yaw sway */
    GESTURE_LOOK_LEFT,   /* the robot's left */
    GESTURE_LOOK_RIGHT,
    GESTURE_HOME,
    GESTURE_PUBLIC_COUNT,        /* the ones Muse may call (robot.gesture) */
    /* Local only: face a talker (experimental direction finding). They end
     * turned, pitch on its rest; GESTURE_HOME brings the head back. */
    GESTURE_TURN_LEFT = GESTURE_PUBLIC_COUNT,
    GESTURE_TURN_RIGHT,
    GESTURE_COUNT,
} gesture_id_t;

typedef struct {
    float yaw, pitch;    /* degrees, see above */
    uint16_t ms;         /* time to get here from the previous frame */
} gesture_frame_t;

typedef struct {
    gesture_frame_t frame[GESTURE_MAX_FRAMES];
    int n;
    uint32_t total_ms;
} gesture_plan_t;

/* "nod", "shake", "sway", "look_left", "look_right", "home"; -1 if unknown. */
int gesture_from_name(const char *name);
const char *gesture_name(gesture_id_t g);

/*
 * Plans g from the current pose (yaw_now, pitch_now in the coordinates above)
 * with repeat (1..GESTURE_MAX_REPEAT, ignored by look and home) and speed
 * (0.5..1.0: slower only). Every plan ends at yaw 0, pitch 0, except the
 * local turns, which end at yaw -+GESTURE_TURN_DEG, pitch 0. Segments are
 * stretched to respect the speed limits. Returns NULL on success, else why
 * the request was refused (bad parameter, a limit, too long).
 */
const char *gesture_plan(gesture_id_t g, int repeat, float speed, float yaw_now, float pitch_now, gesture_plan_t *out);
