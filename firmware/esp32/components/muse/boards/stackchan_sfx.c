/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#include "stackchan_sfx.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PEAK 9000.0f   /* soft: well under the replies' level */

static const float SHY_MS = 320.0f;
static const float HAPPY_NOTE_MS = 85.0f;
static const float HAPPY_HZ[3] = { 1046.5f, 1318.5f, 1568.0f };   /* C6 E6 G6 */

static const float MS[SFX_COUNT] = {
    [SFX_SHY] = 320.0f, [SFX_HAPPY] = 3 * 85.0f + 60.0f, [SFX_DIZZY] = 1100.0f, [SFX_WHOA] = 400.0f,
    [SFX_SLIP] = 350.0f, [SFX_PROTEST] = 450.0f, [SFX_RELIEF] = 500.0f,
};

size_t sfx_frames(sfx_t s)
{
    return s < SFX_COUNT ? (size_t)(MS[s] * SFX_RATE / 1000.0f) : 0;
}

/* Fade in over `in` and out to the end, x = 0..1 through the sound. */
static float fade(float x, float in)
{
    return (x < in ? x / in : 1.0f) * (1.0f - x * x);
}

static float dizzy_sample(float t)
{
    const float T = 1.1f;
    /* 900 -> 400 Hz, a deep 6 Hz wobble: phase = integral of f. */
    float ph = 900.0f * t - 0.5f * (500.0f / T) * t * t + (80.0f / (2.0f * (float)M_PI * 6.0f)) *
               (1.0f - cosf(2.0f * (float)M_PI * 6.0f * t));
    float s = sinf(2.0f * (float)M_PI * ph);
    return (s + 0.25f * sinf(6.0f * (float)M_PI * ph)) * fade(t / T, 0.05f) * 0.8f;
}

static float whoa_sample(float t)
{
    const float T = 0.4f, f0 = 500.0f, r = logf(1300.0f / 500.0f) / T;   /* exponential rise */
    float ph = f0 * (expf(r * t) - 1.0f) / r;
    float tri = 2.0f * fabsf(2.0f * (ph - floorf(ph + 0.5f))) - 1.0f;
    return tri * fade(t / T, 0.1f);
}

static float slip_sample(float t)
{
    const float T = 0.35f, half = 0.15f;
    float f = t < half ? 1200.0f : 800.0f - 150.0f * (t - half) / (T - half);
    static float ph;   /* one sound renders front to back: running phase is fine */
    if (t == 0) {
        ph = 0;
    }
    ph += f / SFX_RATE;
    float u = t < half ? t / half : (t - half) / (T - half);
    float env = (u < 0.08f ? u / 0.08f : 1.0f) * (1.0f - 0.6f * u);
    return sinf(2.0f * (float)M_PI * ph) * env * (t < half ? 0.8f : 1.0f);
}

static float protest_sample(float t)
{
    static const float HZ[3] = { 1400.0f, 1400.0f, 1650.0f };
    int k = (int)(t / 0.15f);
    k = k > 2 ? 2 : k;
    float u = t - k * 0.15f;
    if (u > 0.09f) {
        return 0;
    }
    float s = sinf(2.0f * (float)M_PI * HZ[k] * u);
    float sq = s > 0 ? 0.6f : -0.6f;   /* a little buzz */
    float env = (u < 0.005f ? u / 0.005f : 1.0f) * (u > 0.08f ? (0.09f - u) / 0.01f : 1.0f);
    return (0.6f * s + 0.4f * sq) * env * 0.8f;
}

static uint32_t s_noise = 0x2545F491u;

static float relief_sample(float t)
{
    const float T = 0.5f;
    if (t == 0) {
        s_noise = 0x2545F491u;
    }
    s_noise ^= s_noise << 13;
    s_noise ^= s_noise >> 17;
    s_noise ^= s_noise << 5;
    float n = (float)(int32_t)s_noise / 2147483648.0f;
    float ph = 700.0f * t - 0.5f * (250.0f / T) * t * t;
    return (0.55f * sinf(2.0f * (float)M_PI * ph) + 0.35f * n) * fade(t / T, 0.15f) * 0.8f;
}

/* Phase of a sine whose frequency glides f(t): integrated analytically per piece. */
static float shy_sample(float t)   /* t in s */
{
    /* 0..45 %: 700 -> 1050 Hz; 45..100 %: 1050 -> 820 Hz; a little vibrato throughout. */
    const float T = SHY_MS / 1000.0f, t1 = 0.45f * T;
    float ph;
    if (t < t1) {
        ph = 700.0f * t + 0.5f * (350.0f / t1) * t * t;
    } else {
        float u = t - t1, ph1 = 700.0f * t1 + 0.5f * 350.0f * t1;
        ph = ph1 + 1050.0f * u - 0.5f * (230.0f / (T - t1)) * u * u;
    }
    ph += 3.0f * sinf(2.0f * (float)M_PI * 9.0f * t);   /* vibrato */
    float x = t / T;
    float env = (x < 0.12f ? x / 0.12f : 1.0f) * (1.0f - x) * (1.0f - x) * 1.3f;
    float s = sinf(2.0f * (float)M_PI * ph);
    return (s + 0.15f * sinf(4.0f * (float)M_PI * ph)) * env;
}

static float happy_sample(float t)
{
    float note_s = HAPPY_NOTE_MS / 1000.0f;
    int k = (int)(t / note_s);
    if (k > 2) {
        k = 2;   /* the last note rings on */
    }
    float u = t - k * note_s;
    float len = k == 2 ? note_s + 0.06f : note_s;
    if (u > len) {
        return 0;
    }
    float ph = HAPPY_HZ[k] * u;
    float tri = 2.0f * fabsf(2.0f * (ph - floorf(ph + 0.5f))) - 1.0f;   /* triangle: soft and toy-like */
    float env = (u < 0.004f ? u / 0.004f : 1.0f) * expf(-u / (k == 2 ? 0.07f : 0.04f));
    return tri * env;
}

size_t sfx_render(sfx_t s, size_t from, int16_t *out, size_t n)
{
    size_t total = sfx_frames(s), i = 0;
    for (; i < n && from + i < total; i++) {
        float t = (float)(from + i) / SFX_RATE;
        float v = s == SFX_SHY ? shy_sample(t) : s == SFX_HAPPY ? happy_sample(t) : s == SFX_DIZZY ? dizzy_sample(t)
                  : s == SFX_WHOA ? whoa_sample(t) : s == SFX_SLIP ? slip_sample(t)
                  : s == SFX_PROTEST ? protest_sample(t) : relief_sample(t);
        v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        out[i] = (int16_t)lroundf(v * PEAK);
    }
    return i;
}
