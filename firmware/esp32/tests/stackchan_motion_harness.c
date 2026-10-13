/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Synthetic IMU scenarios at 50 Hz; one line each: "name events dizzy tilt". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "boards/stackchan_motion.h"

#define DT 0.02f
static motion_t m;
static uint32_t evs;
static unsigned rng = 1;

static float noise(float a)
{
    rng = rng * 1103515245u + 12345u;
    return ((rng >> 8) / 16777216.0f - 0.5f) * 2 * a;
}

/* Gravity tilted `roll` degrees about z from +y ("up"), plus extra accel along up. */
static void run(float secs, float roll, float extra_up, float shake_g, float hz, float gyro, bool hold)
{
    for (float t = 0; t < secs; t += DT) {
        float r = roll / 57.29578f, s = shake_g * sinf(2 * 3.14159265f * hz * t);
        float a[3] = { sinf(r) + s + noise(0.01f), cosf(r) * (1 + extra_up) + noise(0.01f), noise(0.01f) };
        float g[3] = { noise(1.0f), noise(1.0f), gyro * sinf(2 * 3.14159265f * hz * t) };
        evs |= motion_update(&m, a, g, DT, hold);
    }
}

static void report(const char *name)
{
    printf("%s %u %.2f %.2f\n", name, (unsigned)evs, m.dizzy, m.tilt);
    evs = 0;
}

static void fresh(void)
{
    motion_init(&m);
    evs = 0;
    run(2.0f, 0, 0, 0, 0, 0, false);   /* sitting still: learns up */
    evs = 0;
}

int main(void)
{
    fresh(); report("rest");
    fresh(); run(2.0f, 30, 0, 0, 0, 0, false); report("tilt30");
    fresh(); run(2.0f, -30, 0, 0, 0, 0, false); report("tilt-30");
    fresh(); run(1.6f, 0, 0, 1.6f, 5, 0, false); report("shake");
    run(1.5f, 0, 0, 0, 0, 0, false); report("after1.5");
    run(3.0f, 0, 0, 0, 0, 0, false); report("after4.5");
    fresh(); run(1.6f, 0, 0, 0, 3, 600, false); report("spin");
    fresh(); run(2.0f, 0, 0, 1.0f, 4, 0, false); report("side1g");
    fresh(); run(2.0f, 0, 0, 0, 3, 250, false); report("twist250");
    fresh(); run(0.3f, 0, 0.9f, 0, 0, 200, false); run(1.0f, 0, 0, 0, 0, 0, false); report("hardlift");
    fresh(); run(2.0f, 0, 0, 2.5f, 4, 260, false); report("realshake");
    fresh(); run(0.16f, 0, 0.45f, 0, 0, 0, false); run(1.0f, 0, 0, 0, 0, 0, false); report("lift");
    fresh(); run(1.2f, 180, 0, 0, 0, 0, false); report("flip");
    run(1.2f, 0, 0, 0, 0, 0, false); report("unflip");
    fresh(); run(2.0f, 60, 0, 0, 0, 0, false); report("slip");
    fresh(); run(1.6f, 0, 0, 1.6f, 5, 600, true); report("hold");
    fresh(); run(3.0f, 0, 0, 0.3f, 2, 60, false); report("gentle");
    fresh(); run(10.0f, 25, 0, 0, 0, 0, false); report("slope");
    return 0;
}
