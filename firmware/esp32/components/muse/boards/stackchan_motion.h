/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * Motion play from the head's IMU (pure, host-tested): shaking makes it dizzy,
 * tilting leans it, a lift surprises it, upside down scares it. Feed one
 * sample per tick (accel in g, gyro in deg/s, device axes; z is the screen's
 * normal). "Up" is learned from the robot sitting still, so the head's own
 * pitch doesn't matter; `hold` (servos moving) freezes recognition.
 */

#include <stdbool.h>
#include <stdint.h>

enum {
    MOTION_SHAKE  = 1 << 0,   /* shaking began */
    MOTION_LIFT   = 1 << 1,   /* picked up from rest */
    MOTION_FLIP   = 1 << 2,   /* turned upside down */
    MOTION_UNFLIP = 1 << 3,   /* the right way up again */
    MOTION_SLIP   = 1 << 4,   /* tilted past MOTION_SLIP_DEG */
};

#define MOTION_TILT_FULL_DEG  45.0f   /* tilt reads +-1 here */
#define MOTION_SLIP_DEG       50.0f
#define MOTION_DIZZY_SECS     3.5f    /* full dizziness wears off in this long */

typedef struct {
    float t;
    float g[3];          /* low-passed accel: gravity */
    float up[3];         /* learned "up" (unit), valid when have_up */
    bool have_up;
    float still_s;
    float e_acc, e_spin; /* mean square of the swing away from gravity (g^2) and of the spin ((deg/s)^2) */
    float hard_s, soft_s;/* above / below the shake level this long */
    float rev_t[4];      /* recent back-and-forth reversals (ring) */
    int nrev;
    int8_t acc_sign[3], spin_sign[3];
    bool shaking;
    float dizzy;         /* 0..1 */
    float tilt;          /* -1..1, + = the robot's right side down */
    bool slipped;
    float slip_s;
    bool flipped;
    float flip_s;
    float above_s;       /* pushed upward this long */
    float rest_s;        /* the last rest lasted this long */
    float since_rest;    /* and ended this long ago */
    float lift_cool;
} motion_t;

void motion_init(motion_t *m);
/* One sample; returns MOTION_* events that began now. */
uint32_t motion_update(motion_t *m, const float acc[3], const float gyro[3], float dt, bool hold);
