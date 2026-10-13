/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * F8 dance, the pure part (host-tested): built-in original tunes rendered by a
 * small synth, and the choreography as a continuous function of the playhead
 * (head pose, base lights, screen colour). No hardware here.
 *
 * Pose: degrees, yaw + = the robot's right as in stackchan_gesture.h, pitch
 * above the rest pose. Times: ms from the first sample of the tune.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DANCE_RATE         16000
#define DANCE_YAW_LIMIT    20.0f    /* inside the gesture limits (25) */
#define DANCE_PITCH_LIMIT  16.0f    /* (30) */
#define DANCE_YAW_SPEED    55.0f    /* deg/s, under the executor's 60 */
#define DANCE_PITCH_SPEED  40.0f    /* (45) */
#define DANCE_MAX_MS       40000
#define DANCE_LIGHT_MAX    110      /* per channel, of 255: soft, never a strobe */

typedef enum {
    DANCE_HAPPY,    /* 欢快, 120 BPM, C major */
    DANCE_CHILL,    /* 慢摇, 96 BPM, A minor */
    DANCE_COUNT,
} dance_song_t;

int dance_from_name(const char *name);           /* -1 if unknown */
const char *dance_name(dance_song_t s);
uint32_t dance_total_ms(dance_song_t s);
float dance_bpm(dance_song_t s);

/* Beats since the start (fractional); negative before it. */
float dance_beats(dance_song_t s, int32_t t_ms);

void dance_pose_at(dance_song_t s, int32_t t_ms, float *yaw, float *pitch);

/* Base lights (RGB888, left LEDs 0-5 and right 6-11) and the screen tint. */
void dance_light_at(dance_song_t s, int32_t t_ms, uint32_t *left, uint32_t *right);
uint32_t dance_tint_at(dance_song_t s, int32_t t_ms);

/* Samples the whole dance every 10 ms: limits, speeds, starts at the lift
 * pose and ends at it. NULL if fine, else why. */
const char *dance_check(dance_song_t s);

/* The first and last pose: facing forward, head just off its rest. */
#define DANCE_START_PITCH  4.0f

typedef struct {
    dance_song_t song;
    uint32_t frame;          /* next frame to render */
    float lead_ph, bass_ph, kick_ph;
    float prev_noise;
    uint32_t noise;
} dance_synth_t;

void dance_synth_init(dance_synth_t *s, dance_song_t song);
/* Renders up to `frames` mono samples; returns how many (0 once it's over). */
size_t dance_synth_render(dance_synth_t *s, int16_t *out, size_t frames);
