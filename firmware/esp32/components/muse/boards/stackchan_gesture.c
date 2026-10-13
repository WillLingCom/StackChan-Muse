/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Limits follow and tighten M5Stack's StackChan
 *  the factory firmware (m5stack/StackChan @1b57655, MIT).
 */

#include "stackchan_gesture.h"

#include <math.h>
#include <string.h>

static const char *const NAMES[GESTURE_COUNT] = {
    [GESTURE_NOD] = "nod",
    [GESTURE_SHAKE] = "shake",
    [GESTURE_SWAY] = "sway",
    [GESTURE_LOOK_LEFT] = "look_left",
    [GESTURE_LOOK_RIGHT] = "look_right",
    [GESTURE_HOME] = "home",
    [GESTURE_TURN_LEFT] = "turn_left",
    [GESTURE_TURN_RIGHT] = "turn_right",
};

int gesture_from_name(const char *name)
{
    for (int i = 0; name && i < GESTURE_COUNT; i++) {
        if (!strcmp(name, NAMES[i])) {
            return i;
        }
    }
    return -1;
}

const char *gesture_name(gesture_id_t g)
{
    return (unsigned)g < GESTURE_COUNT ? NAMES[g] : "?";
}

static bool add(gesture_plan_t *p, float yaw, float pitch, int ms)
{
    if (p->n >= GESTURE_MAX_FRAMES) {
        return false;
    }
    p->frame[p->n++] = (gesture_frame_t){ yaw, pitch, (uint16_t)ms };
    return true;
}

const char *gesture_plan(gesture_id_t g, int repeat, float speed, float yaw_now, float pitch_now, gesture_plan_t *out)
{
    memset(out, 0, sizeof(*out));
    if ((unsigned)g >= GESTURE_COUNT) {
        return "unknown gesture";
    }
    if (repeat < 1 || repeat > GESTURE_MAX_REPEAT) {
        return "repeat must be 1-3";
    }
    if (!isfinite(speed) || speed < 0.5f || speed > 1.0f) {
        return "speed must be 0.5-1.0";
    }
    if (!isfinite(yaw_now) || !isfinite(pitch_now) || fabsf(yaw_now) > 90.0f || pitch_now < -20.0f || pitch_now > 90.0f) {
        return "current pose unknown or implausible";
    }
    const float up = GESTURE_PITCH_LIFT;
    bool ok = true;
    switch (g) {
    case GESTURE_NOD:
        ok &= add(out, 0, 14, 400);
        for (int i = 0; i < repeat; i++) {
            ok &= add(out, 0, 6, 250) && add(out, 0, 14, 250);
        }
        break;
    case GESTURE_SHAKE:
        ok &= add(out, 12, up, 300);
        for (int i = 0; i < repeat; i++) {
            ok &= add(out, -12, up, 450) && add(out, 12, up, 450);
        }
        break;
    case GESTURE_SWAY:
        ok &= add(out, 15, up, 700);
        for (int i = 0; i < repeat; i++) {
            ok &= add(out, -15, up, 1200) && add(out, 15, up, 1200);
        }
        break;
    case GESTURE_LOOK_LEFT:
    case GESTURE_LOOK_RIGHT: {
        float y = g == GESTURE_LOOK_LEFT ? -20.0f : 20.0f;
        ok &= add(out, y, up + 2, 600) && add(out, y, up + 2, 1500);   /* look, hold */
        break;
    }
    case GESTURE_TURN_LEFT:
    case GESTURE_TURN_RIGHT: {
        float y = g == GESTURE_TURN_LEFT ? -GESTURE_TURN_DEG : GESTURE_TURN_DEG;
        ok &= add(out, y, up, 900) && add(out, y, 0, 300);   /* slow turn, then onto the rest */
        break;
    }
    case GESTURE_HOME:
    default:
        break;
    }
    if (g != GESTURE_TURN_LEFT && g != GESTURE_TURN_RIGHT) {
        ok &= add(out, 0, out->n ? up : 0, 500);   /* centre, still off the stop */
        if (out->frame[out->n - 1].pitch != 0) {
            ok &= add(out, 0, 0, 300);             /* settle onto the rest */
        }
    }
    if (!ok) {
        return "too many keyframes";
    }

    /* Slower if asked, then never faster than the speed limits. */
    float py = yaw_now, pp = pitch_now;
    for (int i = 0; i < out->n; i++) {
        gesture_frame_t *f = &out->frame[i];
        if (fabsf(f->yaw) > GESTURE_YAW_LIMIT || f->pitch < 0 || f->pitch > GESTURE_PITCH_LIMIT) {
            return "a keyframe is outside the soft limits";
        }
        float ms = f->ms / speed;
        float need = fmaxf(fabsf(f->yaw - py) / GESTURE_YAW_SPEED, fabsf(f->pitch - pp) / GESTURE_PITCH_SPEED) * 1000.0f;
        if (ms < need) {
            ms = need;
        }
        if (ms < 20) {
            ms = 20;
        }
        f->ms = (uint16_t)ceilf(ms);
        out->total_ms += f->ms;
        py = f->yaw;
        pp = f->pitch;
    }
    if (out->total_ms > GESTURE_MAX_MS) {
        return "longer than 5 s";
    }
    return NULL;
}
