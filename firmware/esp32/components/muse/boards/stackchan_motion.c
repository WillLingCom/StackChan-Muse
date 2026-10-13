/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#include "stackchan_motion.h"

#include <math.h>
#include <string.h>

#define RAD2DEG        57.29578f
#define G_TAU_S        0.25f    /* gravity low-pass */
#define STILL_DEV_G    0.06f
#define STILL_DPS      8.0f
#define UP_FIRST_S     1.0f     /* still this long: learn "up" */
#define UP_ADAPT_S     4.0f     /* still this long: follow a new resting pose */
#define UP_ADAPT_TAU_S 2.0f
#define SHAKE_TAU_S    0.25f    /* the shake level: mean square over about this long */
#define SHAKE_G_RMS    0.55f    /* swing away from gravity, any direction */
#define SHAKE_DPS_RMS  150.0f   /* or a twist */
#define SHAKE_HOLD_S   0.5f     /* that long: shaking (a lift is one short push) */
#define SHAKE_END_S    0.4f     /* below it that long: stopped */
#define DIZZY_PER_S    0.5f     /* dizzier the longer it goes on */
#define REV_G          0.3f     /* a reversal: the swing changes sign past this ... */
#define REV_DPS        80.0f    /* ... or the spin past this */
#define REVERSALS      3        /* within REV_WIN_S: back and forth, not a flip or a lift */
#define REV_WIN_S      1.0f
#define LIFT_G         1.22f    /* along "up" */
#define LIFT_HOLD_S    0.04f
#define LIFT_REST_S    0.5f     /* it rested at least this long ... */
#define LIFT_AFTER_S   0.3f     /* ... ending at most this long before the push */
#define LIFT_COOL_S    3.0f
#define FLIP_G         (-0.6f)
#define UNFLIP_G       0.4f
#define FLIP_HOLD_S    0.8f
#define UNFLIP_HOLD_S  0.5f
#define SLIP_BACK_DEG  35.0f
#define SLIP_HOLD_S    0.3f     /* past MOTION_SLIP_DEG this long, still head-up */
#define SHAKE_QUIET_S  0.5f     /* no swing this long: the shaking stopped */

static float norm3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void set_up(motion_t *m, const float g[3])
{
    float n = norm3(g);
    if (n > 0.5f) {
        for (int i = 0; i < 3; i++) {
            m->up[i] = g[i] / n;
        }
        m->have_up = true;
    }
}

void motion_init(motion_t *m)
{
    memset(m, 0, sizeof(*m));
}

/* Roll about the screen's normal (z) from "up" to gravity now, degrees. */
static float roll_deg(const motion_t *m)
{
    float ux = m->up[0], uy = m->up[1], gx = m->g[0], gy = m->g[1];
    float nu = sqrtf(ux * ux + uy * uy), ng = sqrtf(gx * gx + gy * gy);
    if (nu < 0.3f || ng < 0.3f) {
        return 0;   /* screen facing up or down: no sideways tilt to speak of */
    }
    return atan2f(ux * gy - uy * gx, ux * gx + uy * gy) * RAD2DEG;
}

uint32_t motion_update(motion_t *m, const float acc[3], const float gyro[3], float dt, bool hold)
{
    uint32_t ev = 0;
    if (!(dt > 0) || dt > 0.5f || !isfinite(acc[0] + acc[1] + acc[2] + gyro[0] + gyro[1] + gyro[2])) {
        return 0;
    }
    m->t += dt;
    float k = dt / (G_TAU_S + dt);
    for (int i = 0; i < 3; i++) {
        m->g[i] += (acc[i] - m->g[i]) * k;
    }
    float dev = norm3(acc) - 1.0f, spin = norm3(gyro);
    bool still = fabsf(dev) < STILL_DEV_G && spin < STILL_DPS;
    m->still_s = still ? m->still_s + dt : 0;
    if (still) {
        m->rest_s = m->still_s;   /* how long it rested ... */
        m->since_rest = 0;
    } else {
        m->since_rest += dt;      /* ... and how long ago that ended */
    }

    /* Dizziness wears off whatever else happens. */
    if (!m->shaking) {
        m->dizzy -= dt / MOTION_DIZZY_SECS;
        if (m->dizzy < 0) {
            m->dizzy = 0;
        }
    }
    m->lift_cool -= dt;

    if (hold) {   /* the servos are moving the head: nothing here is the user */
        m->e_acc = m->e_spin = m->hard_s = 0;
        m->nrev = 0;
        m->shaking = false;
        m->above_s = m->rest_s = 0;
        m->since_rest = 1;
        m->still_s = 0;
        m->tilt += (0 - m->tilt) * k;
        return 0;
    }

    /* "Up": first from rest, then following a new resting pose (a slope it was put on). */
    if (!m->have_up && m->still_s >= UP_FIRST_S) {
        set_up(m, m->g);
    } else if (m->have_up && m->still_s >= UP_ADAPT_S && !m->flipped) {
        float a = dt / (UP_ADAPT_TAU_S + dt), v[3];
        for (int i = 0; i < 3; i++) {
            v[i] = m->up[i] + (m->g[i] - m->up[i]) * a;
        }
        set_up(m, v);
    }

    /* Shake: the level of the swing away from gravity (a vector: a sideways
     * shake barely changes the accel's size) or of the spin, held for a while.
     * A hand shaking never pauses between swings, so it's energy, not a count. */
    float hp[3] = { acc[0] - m->g[0], acc[1] - m->g[1], acc[2] - m->g[2] };
    float away = norm3(hp);
    float ks = dt / (SHAKE_TAU_S + dt);
    m->e_acc += (away * away - m->e_acc) * ks;
    m->e_spin += (spin * spin - m->e_spin) * ks;
    bool reversed = false;
    for (int i = 0; i < 3; i++) {
        int8_t sa = hp[i] > REV_G ? 1 : hp[i] < -REV_G ? -1 : 0;
        int8_t sg = gyro[i] > REV_DPS ? 1 : gyro[i] < -REV_DPS ? -1 : 0;
        if (sa && sa == -m->acc_sign[i]) {
            reversed = true;
        }
        if (sg && sg == -m->spin_sign[i]) {
            reversed = true;
        }
        m->acc_sign[i] = sa ? sa : m->acc_sign[i];
        m->spin_sign[i] = sg ? sg : m->spin_sign[i];
    }
    if (reversed) {
        m->rev_t[m->nrev++ % 4] = m->t;
    }
    int recent = 0;
    for (int i = 0; i < 4 && i < m->nrev; i++) {
        recent += m->t - m->rev_t[i] <= REV_WIN_S;
    }
    bool hard = (sqrtf(m->e_acc) > SHAKE_G_RMS || sqrtf(m->e_spin) > SHAKE_DPS_RMS) && recent >= REVERSALS;
    m->hard_s = hard ? m->hard_s + dt : 0;
    m->soft_s = hard ? 0 : m->soft_s + dt;
    if (!m->shaking && m->hard_s >= SHAKE_HOLD_S) {
        m->shaking = true;
        ev |= MOTION_SHAKE;
        m->dizzy = fminf(1.0f, m->dizzy + 0.4f);
    } else if (m->shaking && m->soft_s >= SHAKE_END_S) {
        m->shaking = false;
    }
    if (m->shaking) {
        m->dizzy = fminf(1.0f, m->dizzy + DIZZY_PER_S * dt);
    }

    if (!m->have_up) {
        return ev;
    }

    /* Lift: a push along "up" right after resting (not part of a shake). */
    float along = dot3(acc, m->up);
    if (along > LIFT_G && !m->shaking) {
        m->above_s += dt;
        if (m->above_s >= LIFT_HOLD_S && m->rest_s >= LIFT_REST_S && m->since_rest <= LIFT_AFTER_S &&
            m->lift_cool <= 0) {
            ev |= MOTION_LIFT;
            m->lift_cool = LIFT_COOL_S;
            m->rest_s = 0;
        }
    } else {
        m->above_s = 0;
    }

    /* Upside down: gravity against "up" for a while; back when it's up again. */
    float g_along = dot3(m->g, m->up);
    if (!m->flipped) {
        m->flip_s = g_along < FLIP_G ? m->flip_s + dt : 0;
        if (m->flip_s >= FLIP_HOLD_S) {
            m->flipped = true;
            m->flip_s = 0;
            ev |= MOTION_FLIP;
        }
    } else {
        m->flip_s = g_along > UNFLIP_G ? m->flip_s + dt : 0;
        if (m->flip_s >= UNFLIP_HOLD_S) {
            m->flipped = false;
            m->flip_s = 0;
            ev |= MOTION_UNFLIP;
        }
    }

    /* Tilt (sideways), smoothed; a slip past MOTION_SLIP_DEG. */
    float roll = m->flipped ? 0 : roll_deg(m);
    float want = roll / MOTION_TILT_FULL_DEG;
    want = want > 1 ? 1 : want < -1 ? -1 : want;
    m->tilt += (want - m->tilt) * k;
    if (!m->slipped) {   /* not on the way over (head-down is a flip) nor mid-shake */
        bool past = fabsf(roll) > MOTION_SLIP_DEG && g_along > 0.2f && !m->shaking;
        m->slip_s = past ? m->slip_s + dt : 0;
        if (m->slip_s >= SLIP_HOLD_S) {
            m->slipped = true;
            ev |= MOTION_SLIP;
        }
    } else if (fabsf(roll) < SLIP_BACK_DEG) {
        m->slipped = false;
        m->slip_s = 0;
    }
    return ev;
}
