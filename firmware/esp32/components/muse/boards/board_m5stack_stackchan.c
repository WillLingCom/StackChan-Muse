/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * M5Stack StackChan K151: a CoreS3 main unit on the StackChan body.
 * https://docs.m5stack.com/zh_CN/StackChan
 *
 * The main unit is a CoreS3, so the panel (ILI9342C), touch (FT6336U), AW88298
 * speaker, ES7210 mics and AXP2101 PMU come up through Espressif's
 * m5stack_core_s3 BSP exactly as on board_m5stack_cores3.c; PWR is talk.
 *
 * The body adds, on the same internal I2C bus (port 1, SDA 12, SCL 11), a PY32
 * base controller at 0x6F (servo power "VM EN" on its pin 0, 12 RGB LEDs on
 * pin 13) and an Si12T head-touch chip at 0x68, plus two Feetech SCSCL serial
 * bus servos on UART1 (TX 6, RX 7, 1 Mbaud; yaw ID 1, pitch ID 2). Source:
 * m5stack/StackChan firmware/main/hal (hal_io_expander.cpp, hal_head_touch.cpp,
 * hal_servo.cpp) at 1b57655; see HARDWARE_MAP.md in the project root.
 *
 * Body hardware used so far: the Si12T head touch (stackchan_head.c), which
 * makes Muse shy on screen, and read-only diagnostics (stackchan_body.c):
 * PY32 register reads and servo READ packets, ">body" on the console. Nothing
 * writes to the PY32 or the servos, so the head cannot move.
 */
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_board.h"
#include "muse_audio.h"
#include "muse_mem.h"
#include "muse_pmu.h"
#include "muse_chat.h"
#include "muse_input.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "stackchan_body.h"
#include "stackchan_camera.h"
#include "stackchan_dancer.h"
#include "stackchan_ir.h"
#include "stackchan_rgb.h"
#include "stackchan_sfx.h"
#include "stackchan_imu.h"
#include "stackchan_mic.h"
#if CONFIG_MUSE_STACKCHAN_DOA
#include "muse_audio.h"
#include "muse_voice.h"
#include "nvs.h"
#include "stackchan_doa.h"
#endif
#include "stackchan_gesture.h"
#include "stackchan_head.h"
#include "stackchan_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PMU_KEY_EVERY 2         /* poll the PMU over I2C every 20 ms */

static const char *TAG = "board";
static esp_codec_dev_handle_t s_spk, s_mic;
static bool s_pmu;

#if CONFIG_MUSE_STACKCHAN_PET_NOD
#define PET_NOD_DELAY_US    400000      /* after the hand leaves */
#define PET_NOD_COOLDOWN_US 5000000
static esp_timer_handle_t s_pet_timer;
static bool s_stroked;
static int64_t s_last_pet_nod_us;

static void print_gesture(const char *json, void *ctx);

/* A stroke, then the hand gone: a gentle nod, if nothing else is going on. */
static void pet_nod(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    /* Only a real hand moves the head: weak level-1 strokes also show up with
     * nobody touching (seen 5 s after boot), so the touch must have reached 2. */
    if (stackchan_head_touched() || stackchan_head_touch_peak() < 2 || stackchan_body_moving() ||
        muse_state_mode(NULL) != MUSE_MODE_IDLE || now - s_last_pet_nod_us < PET_NOD_COOLDOWN_US) {
        return;
    }
    s_last_pet_nod_us = now;
    stackchan_body_gesture(GESTURE_NOD, 1, 0.75f, print_gesture, NULL);
}
#endif

/* Reaction sounds, played by the voice task (it owns the speaker) when idle;
 * each at most once per SFX_GAP_US, so a hand holding the robot doesn't chatter. */
#define SFX_GAP_US (1500 * 1000)
static volatile sfx_t s_sfx;

static void sfx_job(bool (*interrupted)(void))
{
    static int16_t buf[MUSE_AUDIO_CHUNK];
    sfx_t s = s_sfx;
    size_t at = 0, n;
    while (!interrupted() && (n = sfx_render(s, at, buf, MUSE_AUDIO_CHUNK)) > 0) {
        muse_audio_write(buf, n);
        at += n;
    }
}

static void play_sfx(sfx_t s)
{
    static int64_t last_us[SFX_COUNT];   /* each its own: a stroke's happy sound follows the touch's shy one */
    int64_t now = esp_timer_get_time();
    if (stackchan_dancer_active() || muse_state_mode(NULL) != MUSE_MODE_IDLE || now - last_us[s] < SFX_GAP_US) {
        return;   /* never over a dance (its job would be replaced), a recording or a reply */
    }
    last_us[s] = now;
    s_sfx = s;
    muse_voice_request_job(sfx_job);
}

/* Motion play (the head's IMU): shaken → dizzy, tilted → leans, lifted →
 * surprised, upside down → scared until righted. Idle only, never while the
 * servos move the head (that would read as the user). */
#define TILT_SIGN 1.0f   /* motion tilt (+ = gravity toward +x) to the avatar's (+ = screen right) */

static bool imu_hold(void)
{
    return stackchan_body_moving() || stackchan_dancer_active() || muse_state_mode(NULL) != MUSE_MODE_IDLE;
}

static void on_motion(uint32_t ev, const motion_t *m)
{
    muse_state_set_motion(m->dizzy, TILT_SIGN * m->tilt, m->flipped || m->slipped ? 1.0f : 0.0f);
    if (!ev) {
        return;
    }
    if (ev & (MOTION_SHAKE | MOTION_LIFT | MOTION_FLIP)) {
        muse_input_wake("motion");   /* picking it up wakes it */
    }
    muse_state_poke();
    if (ev & MOTION_SHAKE) {
        play_sfx(SFX_DIZZY);
    }
    if (ev & MOTION_LIFT) {
        muse_state_react(MUSE_REACT_SURPRISED, 0, false);
        play_sfx(SFX_WHOA);
    }
    if (ev & MOTION_SLIP) {
        play_sfx(SFX_SLIP);
    }
    if (ev & MOTION_FLIP) {
        play_sfx(SFX_PROTEST);
        muse_state_set_caption("%s", "放我下来～");
    }
    if (ev & MOTION_UNFLIP) {
        play_sfx(SFX_RELIEF);
        muse_state_set_caption("%s", "");
    }
}

/* Head touch → on-screen reactions: a hand on the head makes Muse shy for as
 * long as it stays; a stroke also makes it happy and leans it the stroke's
 * way. Lifting the hand lets the blush fade and, after a stroke, Muse nods
 * (CONFIG_MUSE_STACKCHAN_PET_NOD). Motion never starts under a hand. */
static void on_head(stackchan_head_event_t ev)
{
    if (ev == STACKCHAN_HEAD_PRESS && stackchan_body_moving() && !stackchan_dancer_active()) {
        stackchan_body_stop();   /* any hand on the head stops the motors first (a dance checks itself) */
    }
    switch (ev) {
    case STACKCHAN_HEAD_PRESS:
#if CONFIG_MUSE_HANDS_FREE
        /* Hands-free: a touch interrupts Muse thinking or speaking. Idle it never starts listening
         * (holding the robot touches its head); a tap on the screen or the button does that. */
        if (!stackchan_dancer_active() && muse_state_mode(NULL) != MUSE_MODE_IDLE) {
            muse_input_hands_free_tap();
        }
#endif
        muse_state_react(MUSE_REACT_SHY, 0, true);
        play_sfx(SFX_SHY);
        break;
    case STACKCHAN_HEAD_SWIPE_UP:
    case STACKCHAN_HEAD_SWIPE_DOWN:
        muse_state_react(MUSE_REACT_SHY, ev == STACKCHAN_HEAD_SWIPE_UP ? 1 : -1, true);
        /* The hop and hearts aren't gated by the reaction layer: only while
         * idle, so a stroke never decorates a recording or a reply. */
        if (muse_state_mode(NULL) == MUSE_MODE_IDLE) {
            muse_state_make_happy();
            play_sfx(SFX_HAPPY);
#if CONFIG_MUSE_STACKCHAN_PET_NOD
            s_stroked = true;
#endif
        }
        break;
    case STACKCHAN_HEAD_RELEASE:
        muse_state_react_release();
#if CONFIG_MUSE_STACKCHAN_PET_NOD
        if (s_stroked && s_pet_timer) {
            esp_timer_stop(s_pet_timer);
            esp_timer_start_once(s_pet_timer, PET_NOD_DELAY_US);
        }
        s_stroked = false;
#endif
        break;
    default:
        break;
    }
}

static void log_body(void)
{
    stackchan_body_status_t st;
    stackchan_body_read(&st);
    char json[720];
    stackchan_body_json(&st, json, sizeof(json));
    ESP_LOGI(TAG, "body: %s", json);
}

#if CONFIG_MUSE_STACKCHAN_DOA
/*
 * Experimental: face a near talker in hands-free conversations. The mics'
 * raw stereo reaches doa_feed through the audio tap (on the voice task, as
 * do the turn events, so no locking). On a turn's end, a clear left or right
 * turns the head ~20 degrees while Muse thinks; unsure or center: no move.
 * After the conversation the head comes home. Each turn's estimate is logged
 * ("doa: ...") for the accuracy count. Off until the user switches it on.
 */
static doa_t s_doa;
static bool s_doa_on, s_doa_turned;

static void doa_tap(const int16_t *stereo, size_t frames, void *ctx)
{
    (void)ctx;
    doa_feed(&s_doa, stereo, frames);
}

static void doa_tap_install(void)
{
    muse_audio_set_tap(s_doa_on ? doa_tap : NULL, NULL);
}

static void print_gesture(const char *json, void *ctx);

static void on_voice_turn(muse_voice_turn_event_t ev)
{
    if (!s_doa_on) {
        return;
    }
    switch (ev) {
    case MUSE_VOICE_TURN_START:
        doa_begin(&s_doa);
        break;
    case MUSE_VOICE_TURN_END: {
        doa_result_t r = doa_end(&s_doa, CONFIG_MUSE_STACKCHAN_DOA_CENTER_X100 / 100.0f,
                                 CONFIG_MUSE_STACKCHAN_DOA_THRESH_X100 / 100.0f);
        static const char *const SIDE[] = { "unsure", "left", "right", "center" };
        bool can = (r.side == DOA_LEFT || r.side == DOA_RIGHT) && !stackchan_body_moving() &&
                   !stackchan_head_touched();
        ESP_LOGI(TAG, "doa: %s, %d frames, delay %+.2f (IQR %+.2f..%+.2f) -> %s", SIDE[r.side], r.frames,
                 (double)r.median, (double)r.q1, (double)r.q3, can ? "turning" : "no move");
        if (can && stackchan_body_gesture(r.side == DOA_LEFT ? GESTURE_TURN_LEFT : GESTURE_TURN_RIGHT, 1, 0.75f,
                                          print_gesture, NULL)) {
            s_doa_turned = true;
        }
        break;
    }
    case MUSE_VOICE_TURN_CANCEL:
        doa_end(&s_doa, 0, 0);
        break;
    case MUSE_VOICE_SESSION_END:
        if (s_doa_turned && !stackchan_head_touched()) {
            s_doa_turned = !stackchan_body_gesture(GESTURE_HOME, 1, 0.75f, print_gesture, NULL);
        }
        break;
    }
}

static bool doa_get(void)
{
    return s_doa_on;
}

static void doa_set(bool on)
{
    s_doa_on = on;
    nvs_handle_t h;
    if (nvs_open("stackchan", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "doa", on);
        nvs_commit(h);
        nvs_close(h);
    }
    doa_tap_install();
    ESP_LOGI(TAG, "turn to voice %s", on ? "on" : "off");
}

static void doa_start(void)
{
    doa_init(&s_doa);
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("stackchan", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "doa", &v);
        nvs_close(h);
    }
    s_doa_on = v;
    doa_tap_install();
    muse_voice_set_turn_listener(on_voice_turn);
}
#endif

/* Any touch on the screen stops the servos: the screen's touch controller
 * stays reliable while they run (the head's Si12T reads weakly then). */
static void screen_pressed(void)
{
    if (stackchan_dancer_active()) {
        stackchan_dancer_stop();
        muse_input_hold_taps(1500);   /* this tap only stops the dance; it doesn't start a conversation */
    }
    if (stackchan_body_moving()) {
        stackchan_body_stop();
    }
}

static void print_gesture(const char *json, void *ctx)
{
    (void)ctx;
    printf("@gesture %s\n", json);
    fflush(stdout);
}

static void print_nudge(const char *json, void *ctx)
{
    (void)ctx;
    printf("@servo %s\n", json);
    fflush(stdout);
}

/*
 * ">body": the body status as "@body {...}".
 * ">servo.nudge=yaw+", "yaw-" or "pitch+": one supervised 10-degree nudge and
 * back (S3 phase 2a), answered with "@servo {...}" when it ends.
 * ">gesture=NAME[,REPEAT[,SPEED]]": nod, shake, sway, look_left, look_right or
 * home (stackchan_gesture.h), answered with "@gesture {...}" when it ends.
 * ">servo.stop": stops either (so does a hand on the head).
 * ">tool=COMMAND {params}": runs a Muse tool (robot.*) the way link.invoke
 * does, without Muse, answered with "@tool {...}" (once, when it finishes).
 */
static void print_tool(void *ctx, cJSON *result)
{
    (void)ctx;
    char *json = cJSON_PrintUnformatted(result);
    printf("@tool %s\n", json ? json : "{}");
    fflush(stdout);
    cJSON_free(json);
    cJSON_Delete(result);
}

static bool console(const char *line)
{
#if CONFIG_MUSE_STACKCHAN_DOA
    if (!strncmp(line, "doa=", 4)) {
        doa_set(line[4] == '1');
        printf("@doa {\"on\":%s}\n", s_doa_on ? "true" : "false");
        fflush(stdout);
        return true;
    }
#endif
#if CONFIG_MUSE_HANDS_FREE
    if (!strncmp(line, "handsfree=", 10)) {
        muse_settings_set_hands_free(line[10] == '1');
        printf("@handsfree {\"on\":%s}\n", muse_settings_hands_free() ? "true" : "false");
        fflush(stdout);
        return true;
    }
#endif
    if (!strncmp(line, "camera.hflip=", 13)) {
        stackchan_camera_set_hflip(line[13] == '1');
        printf("@camera.hflip {\"on\":%s}\n", line[13] == '1' ? "true" : "false");
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "mic.raw=", 8)) {
        /* Raw stereo from both mic slots, to this machine only, for checking
         * the mics and calibrating direction finding: "@mic.part" lines of
         * base64 (interleaved 16-bit L/R at 16 kHz), then "@mic". */
        size_t frames = 0;
        int16_t *pcm = stackchan_mic_record(atoi(line + 8), &frames);
        if (pcm) {
            size_t bytes = frames * 4, step = 576;   /* multiple of 3: whole base64 groups per line */
            static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const uint8_t *b = (const uint8_t *)pcm;
            char out[step / 3 * 4 + 8];
            for (size_t i = 0; i < bytes; i += step) {
                size_t n = bytes - i < step ? bytes - i : step, o = 0;
                for (size_t j = 0; j < n; j += 3) {
                    uint32_t v = b[i + j] << 16 | (j + 1 < n ? b[i + j + 1] << 8 : 0) | (j + 2 < n ? b[i + j + 2] : 0);
                    out[o++] = B64[v >> 18 & 63];
                    out[o++] = B64[v >> 12 & 63];
                    out[o++] = j + 1 < n ? B64[v >> 6 & 63] : '=';
                    out[o++] = j + 2 < n ? B64[v & 63] : '=';
                }
                out[o] = 0;
                printf("@mic.part {\"at\":%u,\"b64\":\"%s\"}\n", (unsigned)i, out);
            }
            free(pcm);
        }
#if CONFIG_MUSE_STACKCHAN_DOA
        doa_tap_install();   /* the recording borrowed the tap */
#endif
        printf("@mic {\"frames\":%u,\"rate\":16000,\"channels\":2}\n", (unsigned)frames);
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "camera")) {
        /* A photo to this machine, for checking the camera: "@camera.part"
         * lines of base64, then "@camera" with the rest of the result. */
        cJSON *r = stackchan_photo_result();
        cJSON *p = cJSON_GetObjectItem(r, "payload");
        cJSON *d = p ? cJSON_GetObjectItem(p, "data_base64") : NULL;
        if (cJSON_IsString(d)) {
            const char *b = d->valuestring;
            size_t n = strlen(b), step = 768;
            for (size_t i = 0; i < n; i += step) {
                printf("@camera.part {\"at\":%u,\"b64\":\"%.*s\"}\n", (unsigned)i, (int)(n - i < step ? n - i : step), b + i);
            }
            cJSON_DeleteItemFromObject(p, "data_base64");
        }
        muse_state_set_caption("%s", "");
        print_tool(NULL, r);   /* prints "@tool {...}" */
        return true;
    }
    if (!strncmp(line, "tool=", 5)) {
        char cmd[32] = "";
        const char *sp = strchr(line + 5, ' ');
        size_t n = sp ? (size_t)(sp - (line + 5)) : strlen(line + 5);
        memcpy(cmd, line + 5, n < sizeof(cmd) - 1 ? n : sizeof(cmd) - 1);
        cJSON *params = sp ? cJSON_Parse(sp + 1) : NULL;
        static unsigned seq;
        char id[24];
        snprintf(id, sizeof(id), "console-%u", ++seq);
        cJSON *r = stackchan_tools_invoke(cmd, params, id, print_tool, NULL);
        cJSON_Delete(params);
        if (!r) {
            r = cJSON_CreateObject();
            cJSON_AddStringToObject(r, "error", "not a robot tool");
        }
        if (cJSON_IsTrue(cJSON_GetObjectItem(r, "_async"))) {
            cJSON_Delete(r);   /* the final result follows through print_tool */
        } else {
            print_tool(NULL, r);
        }
        return true;
    }
    if (!strncmp(line, "aw=", 3)) {
        /* aw=<p0_1><p1_7>, e.g. aw=10: the base 5 V enables, for finding which one is needed. */
        int p01 = line[3] == '1', p17 = line[4] == '1';
        esp_io_expander_handle_t ioe = bsp_io_expander_init();
        esp_err_t e1 = esp_io_expander_set_level(ioe, IO_EXPANDER_PIN_NUM_1, p01);
        esp_err_t e2 = esp_io_expander_set_level(ioe, IO_EXPANDER_PIN_NUM_15, p17);
        printf("@aw {\"p0_1\":%d,\"p1_7\":%d,\"ok\":%s}\n", p01, p17, e1 == ESP_OK && e2 == ESP_OK ? "true" : "false");
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "ir.pad")) {
        char json[128] = "";
        const char *why = stackchan_ir_padcheck(json, sizeof(json));
        printf("@ir %s%s%s\n", why ? "{\"error\":\"" : json, why ? why : "", why ? "\"}" : "");
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "gpio.ir")) {   /* how the IR pins are routed now */
        gpio_dump_io_configuration(stdout, (1ULL << 5) | (1ULL << 10));
        printf("@gpio {\"done\":true}\n");
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "imu.trace=", 10)) {
        stackchan_imu_trace(atoi(line + 10));
        printf("@imu {\"trace\":true}\n");
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "imu")) {
        char json[320];
        stackchan_imu_json(json, sizeof(json));
        printf("@imu %s\n", json);
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "rgb=", 4) || !strcmp(line, "rgb.off")) {
        /* Base LEDs, for checking them: rgb=RRGGBB[,RRGGBB] (left, right), capped at STACKCHAN_RGB_MAX. */
        unsigned l = 0, r = 0;
        int n = line[3] == '=' ? sscanf(line + 4, "%6x,%6x", &l, &r) : 0;
        bool ok = stackchan_rgb_set(l, n == 2 ? r : l);
        printf("@rgb {\"ok\":%s,\"left\":\"%06x\",\"right\":\"%06x\"}\n", ok ? "true" : "false", l, n == 2 ? r : l);
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "ir.try=", 7)) {
        /* ir.try=NAME,HZ: send a saved code once on another carrier (finding a device's carrier). */
        char name[IR_NAME_MAX + 1] = "";
        unsigned hz = 0;
        sscanf(line + 7, "%31[^,],%u", name, &hz);
        static ir_code_t c;
        const char *why = stackchan_ir_load(name, &c);
        if (!why) {
            c.carrier_hz = (uint16_t)hz;
            why = stackchan_ir_send(&c);
        }
        printf("@ir {\"carrier_hz\":%u,\"result\":\"%s\"}\n", hz, why ? why : "sent");
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "ir.echo=", 8)) {
        char json[512] = "";
        const char *why = stackchan_ir_echo(line + 8, json, sizeof(json));
        printf("@ir %s%s%s\n", why ? "{\"error\":\"" : json, why ? why : "", why ? "\"}" : "");
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "ir.selftest=", 12)) {
        char json[96] = "";
        const char *why = stackchan_ir_selftest(atoi(line + 12), json, sizeof(json));
        printf("@ir %s%s%s\n", why ? "{\"error\":\"" : json, why ? why : "", why ? "\"}" : "");
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "servo.stop")) {
        stackchan_dancer_stop();
        stackchan_body_stop();
        printf("@servo {\"stop\":true}\n");
        fflush(stdout);
        return true;
    }
    if (!strncmp(line, "servo.nudge=", 12)) {
        const char *a = line + 12;
        int axis = !strcmp(a, "yaw+") || !strcmp(a, "yaw-") ? 0 : !strcmp(a, "pitch+") ? 1 : -1;
        int dir = a[strlen(a) - 1] == '-' ? -1 : 1;
        if (axis < 0 || !stackchan_body_nudge(axis, dir, print_nudge)) {
            printf("@servo {\"result\":\"refused: busy or not yaw+, yaw-, pitch+\"}\n");
            fflush(stdout);
        }
        return true;
    }
    if (!strncmp(line, "gesture=", 8)) {
        char name[24] = "";
        int repeat = 1;
        float speed = 1.0f;
        sscanf(line + 8, "%23[^,],%d,%f", name, &repeat, &speed);
        int g = gesture_from_name(name);
        if (g < 0 || !stackchan_body_gesture(g, repeat, speed, print_gesture, NULL)) {
            printf("@gesture {\"result\":\"refused: unknown gesture or busy\"}\n");
            fflush(stdout);
        }
        return true;
    }
    if (strcmp(line, "body") != 0) {
        return false;
    }
    stackchan_body_status_t st;
    stackchan_body_read(&st);
    char json[720];
    stackchan_body_json(&st, json, sizeof(json));
    printf("@body %s\n", json);
    fflush(stdout);
    return true;
}

static esp_err_t init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");
#if CONFIG_MUSE_STACKCHAN_PET_NOD
    const esp_timer_create_args_t pet = { .callback = pet_nod, .name = "pet_nod" };
    esp_timer_create(&pet, &s_pet_timer);
#endif
    /* Optional: the board runs without them. */
    /* AW9523 P0_1 and P1_7 on, as the factory firmware sets them (stackchan.cc
     * Aw9523: P0 0b00000111, P1 0b10001111): the bus/boost 5 V the base's LEDs
     * and the IR emitter need. The BSP's AW9523 driver resets them low. */
    esp_io_expander_handle_t ioe = bsp_io_expander_init();
    if (!ioe || esp_io_expander_set_dir(ioe, IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_15, IO_EXPANDER_OUTPUT) != ESP_OK ||
        esp_io_expander_set_level(ioe, IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_15, 1) != ESP_OK) {
        ESP_LOGW(TAG, "AW9523 P0_1/P1_7 not set: base LEDs and IR may stay dark");
    } else {
        ESP_LOGI(TAG, "AW9523 P0_1, P1_7 on (5 V to the base)");
    }
    stackchan_head_start(bsp_i2c_get_handle(), on_head);
    stackchan_body_start(bsp_i2c_get_handle());
    stackchan_imu_start(bsp_i2c_get_handle(), imu_hold, on_motion);
    log_body();
    stackchan_camera_register();   /* powers up on the first photo */
    /* Only the PMU sees PWR: latch its edges for poll_buttons(). Leave its
     * rails alone; the BSP owns them. */
    esp_err_t err = muse_pmu_init(bsp_i2c_get_handle(), true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PMU unavailable (%s): no talk button or battery status", esp_err_to_name(err));
        return ESP_OK;
    }
    s_pmu = true;
    return ESP_OK;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
#if CONFIG_MUSE_STACKCHAN_DOA
    doa_start();   /* after muse_settings_init: NVS is up */
#endif
    /* Two 6.4 KB internal DMA buffers, not the BSP's default full-height one.
     * Its LVGL port supplies byte swapping and the panel/touch orientation. */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = { .buff_dma = true, .buff_spiram = false },
    };
    cfg.lvgl_port_cfg.task_affinity = MUSE_UI_CORE;
    cfg.lvgl_port_cfg.task_priority = MUSE_UI_PRIORITY;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        return NULL;
    }
    *touch = bsp_display_get_input_dev();
    return *touch ? disp : NULL;
}

static bool display_lock(int timeout_ms)
{
    /* Muse uses -1 for forever; esp_lvgl_port uses 0. */
    return bsp_display_lock(timeout_ms < 0 ? 0 : (uint32_t)timeout_ms);
}

static void set_brightness(int pct)
{
    bsp_display_brightness_set(pct);
}

static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    /* Muse records two slots; the BSP's no-argument default is 22.05 kHz. */
    const i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    ESP_RETURN_ON_ERROR(bsp_audio_init(&cfg), TAG, "duplex audio init");
    *spk = s_spk = bsp_audio_codec_speaker_init();
    *mic = s_mic = bsp_audio_codec_microphone_init();
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    /* ES7210 PGA steps are 3 dB; snap so the UI shows what's applied. */
    db = (db / 3) * 3;
    /* esp_codec_dev rounds 33 dB down to 30; the next real step up is 34.5. */
    esp_codec_dev_set_in_gain(mic, db == 33 ? 34.5f : (float)db);
}

static unsigned poll_buttons(void)
{
    static unsigned tick;
    if (!s_pmu || tick++ % PMU_KEY_EVERY) {
        return 0;
    }
    unsigned key = muse_pmu_poll_key();
    return (key & MUSE_PMU_KEY_PRESS ? MUSE_BTN_TALK_PRESS : 0) |
           (key & MUSE_PMU_KEY_RELEASE ? MUSE_BTN_TALK_RELEASE : 0);
}

static esp_err_t read_power(muse_power_t *out)
{
    return s_pmu ? muse_pmu_read_power(out) : ESP_ERR_INVALID_STATE;
}

static esp_err_t power_off(void)
{
    ESP_RETURN_ON_FALSE(s_pmu, ESP_ERR_INVALID_STATE, TAG, "no PMU");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_off(), TAG, "backlight off");
    bsp_display_lock(0);
    esp_err_t err = lvgl_port_stop();
    bsp_display_unlock();
    ESP_RETURN_ON_ERROR(err, TAG, "display stop");
    if (s_spk) {
        esp_codec_dev_close(s_spk);
    }
    if (s_mic) {
        esp_codec_dev_close(s_mic);
    }
    /* PWR turns it back on. */
    return muse_pmu_power_off();
}

static const muse_board_t s_board = {
    .name = "M5Stack StackChan K151",
    .width = BSP_LCD_H_RES,
    .height = BSP_LCD_V_RES,
    .round = false,
    .touch = true,
    .diagonal_in = 2.0f,
    .talk_button = "power",
    .talk_hint = { LV_ALIGN_TOP_LEFT, 8, 8 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = bsp_display_unlock,
    .set_brightness = set_brightness,
    .audio_init = audio_init,
    .mic_slot = -1,
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    .read_power = read_power,
    .power_off = power_off,
    .console = console,
    .screen_pressed = screen_pressed,
#if CONFIG_MUSE_STACKCHAN_DOA
    .extra_switch = "Turn to voice (beta)",
    .extra_switch_get = doa_get,
    .extra_switch_set = doa_set,
#endif
    .tools_register = stackchan_tools_register,
    .tools_invoke = stackchan_tools_invoke,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
