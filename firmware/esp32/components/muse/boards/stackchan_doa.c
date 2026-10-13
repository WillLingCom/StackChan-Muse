/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

#include "stackchan_doa.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE 16000.0f

static void biquad_set(doa_biquad_t *q, bool high, float fc)
{
    const float w = 2.0f * (float)M_PI * fc / RATE, al = sinf(w) / (2.0f * 0.7071f), c = cosf(w), a0 = 1.0f + al;
    float b0 = high ? (1.0f + c) / 2.0f : (1.0f - c) / 2.0f;
    float b1 = high ? -(1.0f + c) : 1.0f - c;
    *q = (doa_biquad_t){ b0 / a0, b1 / a0, b0 / a0, -2.0f * c / a0, (1.0f - al) / a0, 0, 0, 0, 0 };
}

static float biquad(doa_biquad_t *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;
    return y;
}

void doa_init(doa_t *d)
{
    memset(d, 0, sizeof(*d));
    for (int c = 0; c < 2; c++) {
        biquad_set(&d->hp[c], true, 300.0f);
        biquad_set(&d->lp[c], false, 3400.0f);
    }
    d->floor_db = -65.0f;
}

void doa_begin(doa_t *d)
{
    d->collecting = true;
    d->n = 0;
}

float doa_frame_delay(const float *l, const float *r, size_t n)
{
    /* c[k] = sum l[i] r[i+k]: a peak at k > 0 means r lags, so L heard it first. */
    float c[7];
    for (int k = -3; k <= 3; k++) {
        float acc = 0;
        for (size_t i = 3; i + 3 < n; i++) {
            acc += l[i] * r[i + k];
        }
        c[k + 3] = acc;
    }
    int m = 0;
    for (int k = 1; k < 7; k++) {
        if (c[k] > c[m]) {
            m = k;
        }
    }
    float pk = (float)m;
    if (m > 0 && m < 6) {
        float den = c[m - 1] - 2.0f * c[m] + c[m + 1];
        if (den != 0.0f) {
            pk += 0.5f * (c[m - 1] - c[m + 1]) / den;
        }
    }
    return pk - 3.0f;
}

static void frame(doa_t *d, const int16_t *st)
{
    float l[DOA_FRAME], r[DOA_FRAME];
    double e = 0;
    for (int i = 0; i < DOA_FRAME; i++) {
        l[i] = biquad(&d->lp[0], biquad(&d->hp[0], st[2 * i]));
        r[i] = biquad(&d->lp[1], biquad(&d->hp[1], st[2 * i + 1]));
        e += (double)l[i] * l[i];
    }
    float db = (float)(10.0 * log10(e / DOA_FRAME / (32768.0 * 32768.0) + 1e-15));
    bool loud = db >= d->floor_db + DOA_GATE_DB;
    if (!d->collecting) {
        if (!loud) {   /* follow quiet fast, noise slowly */
            d->floor_db += (db - d->floor_db) * (db < d->floor_db ? 0.2f : 0.02f);
        }
        return;
    }
    if (loud && d->n < DOA_MAX_FRAMES) {
        d->delay[d->n++] = doa_frame_delay(l, r, DOA_FRAME);
    }
}

void doa_feed(doa_t *d, const int16_t *stereo, size_t frames)
{
    while (frames) {
        size_t take = DOA_FRAME - d->pend_n < frames ? DOA_FRAME - d->pend_n : frames;
        memcpy(d->pend + 2 * d->pend_n, stereo, take * 2 * sizeof(int16_t));
        d->pend_n += take;
        stereo += 2 * take;
        frames -= take;
        if (d->pend_n == DOA_FRAME) {
            frame(d, d->pend);
            d->pend_n = 0;
        }
    }
}

static int cmp(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

doa_result_t doa_end(doa_t *d, float center, float threshold)
{
    d->collecting = false;
    doa_result_t r = { .side = DOA_UNSURE, .frames = d->n };
    if (d->n == 0) {
        return r;
    }
    float s[DOA_MAX_FRAMES];
    memcpy(s, d->delay, d->n * sizeof(float));
    qsort(s, d->n, sizeof(float), cmp);
    r.median = s[d->n / 2];
    r.q1 = s[d->n / 4];
    r.q3 = s[(3 * d->n) / 4];
    if (d->n >= DOA_MIN_FRAMES) {
        r.side = r.median < center - threshold ? DOA_LEFT : r.median > center + threshold ? DOA_RIGHT : DOA_CENTER;
    }
    return r;
}
