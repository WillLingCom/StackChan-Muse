/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#include "stackchan_dancer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "muse_audio.h"
#include "muse_input.h"
#include "muse_state.h"
#include "muse_voice.h"
#include "stackchan_body.h"
#include "stackchan_dance.h"
#include "stackchan_head.h"
#include "stackchan_rgb.h"

#define LATENCY_MS     90      /* I2S DMA: 6 x 240 frames are queued when a write returns */
#define LEAD_MS        80      /* the servos trail their goals by ~50-150 ms */
#define STALL_MS       500     /* no audio written this long: the music stopped */
#define READY_WAIT_MS  4000    /* the head's glide to the first pose */
#define EXTRAPOLATE_MS 40      /* between writes the playhead runs on, this far at most */

static const char *TAG = "dance";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_active, s_abort, s_ready, s_started, s_music_over, s_body_done;
static volatile int s_parts;   /* the music job and the head; the last one to finish cleans up */
static int s_song;
static int32_t s_head_ms;      /* playhead at s_head_us */
static int64_t s_head_us;
static int s_tick;
static char s_last[96] = "none";
static bool s_outcome;        /* the first outcome wins */

/* A hand, not a blip: firm, or level 1 held 300 ms (as the follow executor judges it). */
static bool hand_on(void)
{
    return stackchan_head_touched() && (stackchan_head_firm() || stackchan_head_touched_ms() >= 300);
}

static void set_last(const char *outcome)
{
    taskENTER_CRITICAL(&s_lock);
    if (!s_outcome) {
        snprintf(s_last, sizeof(s_last), "%s: %s", dance_name((dance_song_t)s_song), outcome);
        s_outcome = true;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void set_head(int32_t ms)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    s_head_ms = ms;
    s_head_us = now;
    taskEXIT_CRITICAL(&s_lock);
}

/* ms into the tune now; *age = since the last write. */
static int32_t playhead(int64_t *age_us)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    int32_t base = s_head_ms;
    int64_t at = s_head_us;
    taskEXIT_CRITICAL(&s_lock);
    int64_t el = (now - at) / 1000;
    *age_us = now - at;
    return base + (int32_t)(el > EXTRAPOLATE_MS ? EXTRAPOLATE_MS : el);
}

static void finish_part(void)
{
    if (__atomic_sub_fetch(&s_parts, 1, __ATOMIC_ACQ_REL) == 0) {
        stackchan_rgb_off();
        muse_state_set_dance(false, 0, 0);
        ESP_LOGI(TAG, "over: %s", s_last);
        s_active = false;
    }
}

/* On the follow task, every 20 ms. */
static int pose(void *ctx, float *yaw, float *pitch, const char **why)
{
    (void)ctx;
    dance_song_t song = (dance_song_t)s_song;
    if (s_abort) {
        *why = "stopped";
        return -1;
    }
    if (!s_started) {
        dance_pose_at(song, 0, yaw, pitch);   /* hold the first pose until the music starts */
        return 1;
    }
    int64_t age;
    int32_t t = playhead(&age);
    if (s_music_over && t >= (int32_t)dance_total_ms(song)) {
        return 0;
    }
    if (!s_music_over && age > STALL_MS * 1000) {
        *why = "music stalled";
        return -1;
    }
    dance_pose_at(song, t + LEAD_MS, yaw, pitch);
    if (++s_tick % 2 == 0) {   /* lights and screen at 25 Hz */
        uint32_t l, r;
        dance_light_at(song, t, &l, &r);
        stackchan_rgb_set(l, r);
        muse_state_set_dance(true, dance_beats(song, t), dance_tint_at(song, t));
    }
    if (s_tick % 100 == 0) {
        muse_state_poke();   /* keep the screen up */
    }
    return 1;
}

static void ready(void *ctx)
{
    (void)ctx;
    s_ready = true;
}

static void body_done(const char *json, void *ctx)
{
    (void)ctx;
    cJSON *r = cJSON_Parse(json);
    const cJSON *res = r ? cJSON_GetObjectItem(r, "result") : NULL;
    const char *outcome = cJSON_IsString(res) ? res->valuestring : "unknown";
    set_last(outcome);
    cJSON_Delete(r);
    s_body_done = true;
    if (s_ready) {   /* the music job is the other part */
        s_abort = s_abort || !s_music_over;
    }
    finish_part();
}

/* On the voice task, once the turn is over. */
static void job(bool (*interrupted)(void))
{
    dance_song_t song = (dance_song_t)s_song;
    const char *why = NULL;
    if (s_abort) {
        why = "stopped before it began";
    } else if (hand_on()) {
        why = "refused: hand on the head";
    } else {
        __atomic_add_fetch(&s_parts, 1, __ATOMIC_ACQ_REL);
        if (!stackchan_body_follow(pose, ready, dance_total_ms(song) + READY_WAIT_MS + 2000, body_done, NULL)) {
            __atomic_sub_fetch(&s_parts, 1, __ATOMIC_ACQ_REL);
            why = "busy: another move is running";
        }
    }
    if (!why) {
        muse_input_wake("dance");
        muse_state_set_caption("%s", "准备跳舞…");
        int64_t until = esp_timer_get_time() + READY_WAIT_MS * 1000;
        while (!s_ready && !s_body_done && !s_abort && !interrupted() && esp_timer_get_time() < until) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!s_ready) {
            s_abort = true;   /* the head stops on its own if it was still getting there */
            why = s_body_done ? "the head could not start" : "stopped";
        }
    }
    if (why) {
        set_last(why);
        muse_state_set_caption("%s", "");
        finish_part();
        return;
    }
    muse_state_set_caption("%s", "跳舞中（点屏幕或摸头停止）");
    static dance_synth_t synth;
    static int16_t buf[MUSE_AUDIO_CHUNK];
    dance_synth_init(&synth, song);
    uint32_t written = 0;
    set_head(-LATENCY_MS);
    s_started = true;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        if (s_abort || s_body_done) {
            why = "stopped";
            break;
        }
        if (interrupted()) {
            why = "a press";
            break;
        }
        if (hand_on()) {   /* the head stops itself; the music shouldn't wait for it */
            why = "hand on the head";
            break;
        }
        size_t n = dance_synth_render(&synth, buf, MUSE_AUDIO_CHUNK);
        if (!n) {
            break;
        }
        if (muse_audio_write(buf, n) != ESP_OK) {
            why = "audio write failed";
            break;
        }
        written += n;
        set_head((int32_t)(written * 1000ull / MUSE_AUDIO_RATE) - LATENCY_MS);
    }
    if (why) {
        s_abort = true;
        memset(buf, 0, sizeof(buf));
        muse_audio_write(buf, MUSE_AUDIO_CHUNK);   /* end on silence, not a click */
        set_last(why);
        muse_state_set_caption("%s", "");
    } else {
        set_head((int32_t)dance_total_ms(song));
        s_music_over = true;
        muse_state_set_caption("%s", "");
    }
    ESP_LOGI(TAG, "music %s after %d ms (%u frames)", why ? why : "done",
             (int)((esp_timer_get_time() - t0) / 1000), (unsigned)written);
    finish_part();
}

const char *stackchan_dancer_start(int song)
{
    if (song < 0 || song >= DANCE_COUNT) {
        return "unknown dance";
    }
    const char *bad = dance_check((dance_song_t)song);
    if (bad) {
        return bad;
    }
    if (muse_state_mode(NULL) == MUSE_MODE_LISTENING) {
        return "busy: the robot is recording the user";
    }
    if (hand_on()) {
        return "refused: someone is touching the robot's head";
    }
    if (stackchan_body_moving()) {
        return "busy: another move is running";
    }
    if (__atomic_exchange_n(&s_active, true, __ATOMIC_ACQ_REL)) {
        return "busy: already dancing";
    }
    s_song = song;
    s_abort = s_ready = s_started = s_music_over = s_body_done = false;
    s_tick = 0;
    s_outcome = false;
    s_parts = 1;   /* the job; it adds the head when it starts it */
    taskENTER_CRITICAL(&s_lock);
    snprintf(s_last, sizeof(s_last), "%s: waiting for the reply to finish", dance_name((dance_song_t)song));
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "%s queued (%u ms)", dance_name((dance_song_t)song), (unsigned)dance_total_ms((dance_song_t)song));
    muse_voice_request_job(job);
    return NULL;
}

void stackchan_dancer_stop(void)
{
    if (s_active && !s_abort) {
        ESP_LOGI(TAG, "stop requested");
        s_abort = true;
    }
}

bool stackchan_dancer_active(void)
{
    return s_active;
}

void stackchan_dancer_last(char *out, size_t cap)
{
    taskENTER_CRITICAL(&s_lock);
    snprintf(out, cap, "%s", s_last);
    taskEXIT_CRITICAL(&s_lock);
}
