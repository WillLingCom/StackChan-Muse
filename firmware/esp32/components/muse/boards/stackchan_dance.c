/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/*
 * Two short original tunes (written for this port, no samples) and their
 * dances. The music is a grid of eighth notes: a lead line, a bass that
 * follows the chord root, and kick/snare/hat. The dance is a function of the
 * beat per section (bob, sway, wiggle, wind down, final nod), cross-faded over
 * the first beat of each section, so the head always follows the playhead
 * smoothly and dance_check() can sample it.
 */
#include "stackchan_dance.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { SEC_INTRO, SEC_A, SEC_B, SEC_OUTRO, SEC_END, SEC_COUNT } sec_t;

#define H -1   /* hold the previous note */
#define R 0    /* rest */

typedef struct {
    const char *name;
    float bpm;
    int bars;
    bool soft;                  /* triangle lead, lighter drums */
    const int8_t (*lead)[8];    /* per bar: eighth notes, MIDI */
    const int8_t *root;         /* per bar: the chord root for the bass, MIDI */
    const uint8_t *sec;         /* per bar */
    uint32_t color[SEC_COUNT];
} song_t;

/* 欢快: C G Am F, 120 BPM, 12 bars (24 s). */
static const int8_t HAPPY_LEAD[12][8] = {
    { R, R, R, R, R, R, R, R },
    { R, R, R, R, R, R, R, R },
    { 64, 67, 72, 67, 64, 67, 72, H },
    { 74, 71, 67, 71, 74, H, 72, 71 },
    { 72, 69, 64, 69, 72, H, 71, 69 },
    { 69, 65, 72, 69, 67, H, 64, 62 },
    { 79, 76, 79, 76, 72, 74, 76, H },
    { 74, H, 71, 74, 79, H, 77, 76 },
    { 76, 72, 76, 81, 79, 76, 72, H },
    { 77, 76, 74, 72, 71, H, 67, H },
    { 72, 67, 64, 67, 72, H, H, H },
    { 72, H, H, H, R, R, R, R },
};
static const int8_t HAPPY_ROOT[12] = { 48, 55, 48, 55, 57, 53, 48, 55, 57, 53, 48, 48 };
static const uint8_t HAPPY_SEC[12] = { SEC_INTRO, SEC_INTRO, SEC_A, SEC_A, SEC_A, SEC_A,
                                       SEC_B, SEC_B, SEC_B, SEC_B, SEC_OUTRO, SEC_END };

/* 慢摇: Am F C G, 96 BPM, 10 bars (25 s). */
static const int8_t CHILL_LEAD[10][8] = {
    { R, R, R, R, R, R, R, R },
    { R, R, R, R, R, R, R, R },
    { 69, H, 72, H, 76, H, 74, 72 },
    { 72, H, 69, H, 65, H, 67, 69 },
    { 67, H, 72, H, 79, H, 76, 74 },
    { 74, H, 71, H, 67, H, H, H },
    { 76, 74, 72, 74, 76, H, 81, H },
    { 77, 76, 74, 72, 69, H, H, H },
    { 72, H, 67, H, 64, H, H, H },
    { 69, H, H, H, R, R, R, R },
};
static const int8_t CHILL_ROOT[10] = { 57, 53, 57, 53, 48, 55, 57, 53, 48, 57 };
static const uint8_t CHILL_SEC[10] = { SEC_INTRO, SEC_INTRO, SEC_A, SEC_A, SEC_A, SEC_A,
                                       SEC_B, SEC_B, SEC_OUTRO, SEC_END };

static const song_t SONGS[DANCE_COUNT] = {
    [DANCE_HAPPY] = { "happy", 120.0f, 12, false, HAPPY_LEAD, HAPPY_ROOT, HAPPY_SEC,
                      { 0x3060FF, 0xFF8040, 0xFF40A0, 0x40FFA0, 0xFFD040 } },
    [DANCE_CHILL] = { "chill", 96.0f, 10, true, CHILL_LEAD, CHILL_ROOT, CHILL_SEC,
                      { 0x4030C0, 0x2090FF, 0xA040FF, 0x30C0A0, 0x6040FF } },
};

int dance_from_name(const char *name)
{
    for (int i = 0; name && i < DANCE_COUNT; i++) {
        if (!strcmp(name, SONGS[i].name)) {
            return i;
        }
    }
    return -1;
}

const char *dance_name(dance_song_t s)
{
    return s < DANCE_COUNT ? SONGS[s].name : "?";
}

float dance_bpm(dance_song_t s)
{
    return SONGS[s].bpm;
}

uint32_t dance_total_ms(dance_song_t s)
{
    return (uint32_t)lroundf(SONGS[s].bars * 4 * 60000.0f / SONGS[s].bpm);
}

float dance_beats(dance_song_t s, int32_t t_ms)
{
    return t_ms * SONGS[s].bpm / 60000.0f;
}

/* ---- choreography ---- */

static float cos01(float x)   /* 0 on the beat, 1 half way between */
{
    return 0.5f - 0.5f * cosf(2.0f * (float)M_PI * x);
}

/* One section's move, bb beats into it (a section is a whole number of bars). */
static void pattern(sec_t sec, float bb, float *yaw, float *pitch)
{
    const float S = DANCE_START_PITCH;
    switch (sec) {
    case SEC_INTRO:   /* bob with the beat */
        *yaw = 0;
        *pitch = S + 5.0f * cos01(bb);
        break;
    case SEC_A:       /* sway over a bar, bob on the beat */
        *yaw = 14.0f * sinf(2.0f * (float)M_PI * bb / 4.0f);
        *pitch = S + 4.0f * cos01(bb);
        break;
    case SEC_B:       /* quicker wiggle, head up a little */
        *yaw = 7.0f * sinf(2.0f * (float)M_PI * bb / 2.0f);
        *pitch = S + 4.0f + 4.0f * cos01(bb);
        break;
    case SEC_OUTRO: { /* wind down over the bar */
        float k = bb >= 4.0f ? 0 : 1.0f - bb / 4.0f;
        *yaw = 6.0f * sinf(2.0f * (float)M_PI * bb / 4.0f) * k;
        *pitch = S + 5.0f * cos01(bb) * k;
        break;
    }
    default:          /* one nod on the final hit, then still */
        *yaw = 0;
        *pitch = bb < 1.0f ? S + 6.0f * sinf((float)M_PI * bb) : S;
        break;
    }
}

/* The section at a bar and the beat it started on. */
static sec_t section_at(const song_t *g, int bar, float *start_beat)
{
    int first = bar;
    while (first > 0 && g->sec[first - 1] == g->sec[bar]) {
        first--;
    }
    *start_beat = first * 4.0f;
    return (sec_t)g->sec[bar];
}

void dance_pose_at(dance_song_t s, int32_t t_ms, float *yaw, float *pitch)
{
    const song_t *g = &SONGS[s];
    float b = dance_beats(s, t_ms);
    if (t_ms <= 0 || b >= g->bars * 4.0f) {
        *yaw = 0;
        *pitch = DANCE_START_PITCH;
        return;
    }
    int bar = (int)(b / 4.0f);
    float start;
    sec_t sec = section_at(g, bar, &start);
    float bb = b - start;
    pattern(sec, bb, yaw, pitch);
    if (start > 0 && bb < 1.0f) {   /* cross-fade from where the last section would be */
        float pstart;
        sec_t prev = section_at(g, (int)(start / 4.0f) - 1, &pstart);
        float py, pp;
        pattern(prev, b - pstart, &py, &pp);
        float w = bb * bb * (3.0f - 2.0f * bb);
        *yaw = py + (*yaw - py) * w;
        *pitch = pp + (*pitch - pp) * w;
    }
}

static uint32_t scale_rgb(uint32_t rgb, float k)
{
    if (k < 0) {
        k = 0;
    }
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float c = ((rgb >> sh) & 0xFF) * k * (DANCE_LIGHT_MAX / 255.0f);
        if (c > DANCE_LIGHT_MAX) {
            c = DANCE_LIGHT_MAX;
        }
        out |= (uint32_t)lroundf(c) << sh;
    }
    return out;
}

/* Section colour and a soft pulse (peaks on the beat; every other beat when soft), 0 outside the tune. */
static uint32_t pulse_color(dance_song_t s, int32_t t_ms, float *pulse)
{
    const song_t *g = &SONGS[s];
    float b = dance_beats(s, t_ms);
    float end = g->bars * 4.0f;
    if (t_ms < 0 || b >= end) {
        *pulse = 0;
        return 0;
    }
    float start;
    sec_t sec = section_at(g, (int)(b / 4.0f), &start);
    float period = g->soft ? 2.0f : 1.0f;
    float x = b / period;
    float p = 0.35f + 0.65f * (0.5f + 0.5f * cosf(2.0f * (float)M_PI * (x - floorf(x))));
    float fade = b < 1.0f ? b : end - b < 2.0f ? (end - b) / 2.0f : 1.0f;   /* in over a beat, out over two */
    *pulse = p * fade;
    return g->color[sec];
}

void dance_light_at(dance_song_t s, int32_t t_ms, uint32_t *left, uint32_t *right)
{
    float p, yaw, pitch;
    uint32_t c = pulse_color(s, t_ms, &p);
    dance_pose_at(s, t_ms, &yaw, &pitch);
    float bal = yaw / DANCE_YAW_LIMIT;   /* the side it leans to glows a little more */
    *left = scale_rgb(c, p * (0.7f - 0.3f * bal));
    *right = scale_rgb(c, p * (0.7f + 0.3f * bal));
}

uint32_t dance_tint_at(dance_song_t s, int32_t t_ms)
{
    float p;
    uint32_t c = pulse_color(s, t_ms, &p);
    return scale_rgb(c, p * 0.5f);
}

const char *dance_check(dance_song_t s)
{
    if (s >= DANCE_COUNT) {
        return "unknown dance";
    }
    uint32_t total = dance_total_ms(s);
    if (total > DANCE_MAX_MS) {
        return "longer than the limit";
    }
    float y0, p0;
    dance_pose_at(s, 0, &y0, &p0);
    if (y0 != 0 || p0 != DANCE_START_PITCH) {
        return "does not start at the lift pose";
    }
    for (uint32_t t = 10; t <= total; t += 10) {
        float y, p;
        dance_pose_at(s, (int32_t)t, &y, &p);
        if (!isfinite(y) || !isfinite(p) || fabsf(y) > DANCE_YAW_LIMIT || p < 0 || p > DANCE_PITCH_LIMIT) {
            return "pose outside the limits";
        }
        if (fabsf(y - y0) / 0.01f > DANCE_YAW_SPEED || fabsf(p - p0) / 0.01f > DANCE_PITCH_SPEED) {
            return "faster than the limits";
        }
        y0 = y;
        p0 = p;
    }
    if (fabsf(y0) > 0.01f || fabsf(p0 - DANCE_START_PITCH) > 0.01f) {
        return "does not end at the lift pose";
    }
    return NULL;
}

/* ---- synth ---- */

static float midi_hz(int n)
{
    return 440.0f * powf(2.0f, (n - 69) / 12.0f);
}

static float noise(dance_synth_t *s)
{
    s->noise ^= s->noise << 13;
    s->noise ^= s->noise >> 17;
    s->noise ^= s->noise << 5;
    return (float)(int32_t)s->noise / 2147483648.0f;
}

void dance_synth_init(dance_synth_t *s, dance_song_t song)
{
    memset(s, 0, sizeof(*s));
    s->song = song;
    s->noise = 0x6B151u;
}

/* The note sounding at eighth `e` of a bar: its MIDI number (0 = rest), and
 * where it began and ends, in eighths. */
static int note_at(const int8_t *bar, int e, int *from, int *to)
{
    int k = e;
    while (k > 0 && bar[k] == H) {
        k--;
    }
    int end = e + 1;
    while (end < 8 && bar[end] == H) {
        end++;
    }
    *from = k;
    *to = end;
    return bar[k] == H ? R : bar[k];
}

/* Attack 5 ms, decay to `sustain`, release over the last 20 ms. */
static float envelope(float age, float left, float tau, float sustain)
{
    float a = age < 80 ? age / 80.0f : 1.0f;
    float d = sustain + (1.0f - sustain) * expf(-age / tau);
    float r = left < 320 ? left / 320.0f : 1.0f;
    return a * d * r;
}

size_t dance_synth_render(dance_synth_t *s, int16_t *out, size_t frames)
{
    const song_t *g = &SONGS[s->song];
    const uint32_t step = (uint32_t)lroundf(DANCE_RATE * 60.0f / g->bpm / 2.0f);   /* an eighth */
    const uint32_t total = step * 8 * g->bars;
    size_t n = 0;
    for (; n < frames && s->frame < total; n++, s->frame++) {
        uint32_t f = s->frame;
        int bar = (int)(f / (step * 8));
        int e = (int)(f / step % 8);
        float pos = (float)(f % step);
        sec_t sec = (sec_t)g->sec[bar];
        float x = 0;

        /* Lead. */
        int from, to;
        int note = note_at(g->lead[bar], e, &from, &to);
        if (note) {
            float age = (e - from) * (float)step + pos;
            float left = (to - e) * (float)step - pos;
            s->lead_ph += midi_hz(note) / DANCE_RATE;
            s->lead_ph -= floorf(s->lead_ph);
            float w = g->soft ? 4.0f * fabsf(s->lead_ph - 0.5f) - 1.0f : (s->lead_ph < 0.25f ? 1.0f : -1.0f);
            x += w * envelope(age, left, 1600.0f, 0.55f) * (g->soft ? 0.30f : 0.20f);
        }

        /* Bass: octave bounce on eighths (happy), on the beat (chill); one long note at the end. */
        bool end_bar = sec == SEC_END;
        {
            int bn = g->root[bar] + (!g->soft && !end_bar && (e & 1) ? 12 : 0);
            float age = end_bar ? f % (step * 8) : g->soft ? (float)(f % (step * 2)) : pos;
            float left = end_bar ? 4.0f * step - age : g->soft ? 2.0f * step - age : step - pos;
            if (left > 0) {
                s->bass_ph += midi_hz(bn) / DANCE_RATE;
                s->bass_ph -= floorf(s->bass_ph);
                float w = g->soft ? 4.0f * fabsf(s->bass_ph - 0.5f) - 1.0f : (s->bass_ph < 0.5f ? 1.0f : -1.0f);
                x += w * envelope(age, left, end_bar ? 6000.0f : 2400.0f, 0.3f) * (g->soft ? 0.26f : 0.13f);
            }
        }

        /* Drums. */
        float hp = noise(s);
        float hat_noise = (hp - s->prev_noise) * 0.5f;   /* first difference: a bright hiss */
        s->prev_noise = hp;
        float beat_age = (float)(f % (step * 2));
        int beat = e / 2;
        if (end_bar) {
            if (beat == 0) {
                float a = (float)(f % (step * 8));
                if (a == 0) {
                    s->kick_ph = 0;
                }
                if (a < 2400) {
                    s->kick_ph += (50.0f + 110.0f * expf(-a / 400.0f)) / DANCE_RATE;
                    x += sinf(2.0f * (float)M_PI * s->kick_ph) * expf(-a / 1400.0f) * 0.5f;
                }
            }
            float a = (float)(f % (step * 8));
            x += hat_noise * expf(-a / 3000.0f) * 0.10f;   /* a soft crash */
        } else {
            if (beat == 0 || beat == 2) {
                if (beat_age == 0) {
                    s->kick_ph = 0;
                }
                if (beat_age < 2400) {
                    s->kick_ph += (50.0f + 110.0f * expf(-beat_age / 400.0f)) / DANCE_RATE;
                    x += sinf(2.0f * (float)M_PI * s->kick_ph) * expf(-beat_age / 1400.0f) * (g->soft ? 0.40f : 0.50f);
                }
            } else if (!(sec == SEC_INTRO && bar == 0) && beat_age < 2400) {
                float snare = hp * expf(-beat_age / 900.0f) * 0.22f +
                              sinf(2.0f * (float)M_PI * 190.0f * beat_age / DANCE_RATE) * expf(-beat_age / 600.0f) * 0.12f;
                x += g->soft ? snare * 0.6f : snare;
            }
            if (!g->soft || (e & 1)) {
                x += hat_noise * expf(-pos / 220.0f) * (g->soft ? 0.06f : 0.08f);
            }
        }

        x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
        float y = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);   /* soft clip */
        out[n] = (int16_t)lroundf(y * 20000.0f);
    }
    return n;
}
