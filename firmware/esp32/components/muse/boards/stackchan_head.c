/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The Si12T register sequence and gesture thresholds follow M5Stack's StackChan
 * firmware (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD).
 */

#include "stackchan_head.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"


#define SI12T_ADDR      0x68
#define SI12T_HZ        100000
#define SI12T_SENS1     0x02    /* .. 0x06: per-channel sensitivity */
#define SI12T_CTRL1     0x08
#define SI12T_CTRL2     0x09
#define SI12T_REF_RST1  0x0A    /* .. 0x0F: reference reset, channel and calibration hold */
#define SI12T_OUTPUT1   0x10
#define SI12T_SENS_LOW3 0x33    /* "low" type, level 3, as the factory firmware */
#define POLL_MS         50
#define TIMEOUT_MS      20
#define MAX_ERRORS      20      /* consecutive read failures before giving up */
#define STUCK_MS        20000   /* touched this long: the chip's references drifted (seen
                                 * after 14 h: 0x22/0x23/0x26 for minutes, nobody touching);
                                 * set it up again so it recalibrates */
#define SETTLE_MS       1500    /* after setup the zones read as touched while the
                                 * references settle (seen at boot: 0x15, all three
                                 * at level 1, with nobody touching); ignore them */

#define FLICKER_MS      400     /* a "touch" this short reading only level 1 */
#define FLICKERS        4       /* this many within FLICKER_SPAN_MS: drifted references */
#define FLICKER_SPAN_MS 8000    /* (seen after a 24 s dance: one every 1.16 s, nobody touching) */

static const char *TAG = "head";
static i2c_master_dev_handle_t s_dev;
static stackchan_head_cb_t s_cb;
static volatile bool s_touched, s_firm;
static volatile uint8_t s_peak;   /* strongest zone level this touch */
static volatile int s_touch_ms;   /* how long the current touch has lasted */

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, sizeof(b), TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, TIMEOUT_MS);
}

/* hal/drivers/Si12T/Si12T.cpp si12t_setup(LOW, LEVEL_3): channels and
 * calibration on, soft reset then sleep-enabled auto mode, CTRL1 0x22 (auto,
 * FTC 01, middle/high interrupt, response 4), then the sensitivity. */
static esp_err_t setup(void)
{
    for (uint8_t r = SI12T_REF_RST1; r <= SI12T_REF_RST1 + 5; r++) {
        ESP_RETURN_ON_ERROR(wr(r, 0x00), TAG, "reg 0x%02x", r);
    }
    ESP_RETURN_ON_ERROR(wr(SI12T_CTRL2, 0x0F), TAG, "ctrl2 reset");
    ESP_RETURN_ON_ERROR(wr(SI12T_CTRL2, 0x07), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(SI12T_CTRL1, 0x22), TAG, "ctrl1");
    for (uint8_t r = SI12T_SENS1; r <= SI12T_SENS1 + 4; r++) {
        ESP_RETURN_ON_ERROR(wr(r, SI12T_SENS_LOW3), TAG, "sens 0x%02x", r);
    }
    return ESP_OK;
}

static void head_task(void *arg)
{
    (void)arg;
    stackchan_head_gesture_t g;
    stackchan_head_gesture_reset(&g);
    int errors = 0;
    int touched_ms = 0;
    int now_ms = 0;                     /* task time, POLL_MS a step */
    int flicker_at[FLICKERS] = { 0 };   /* when the last few flickers ended */
    int nflicker = 0;
    bool only_settling = true;          /* this touch read only level 1 so far */
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    for (;;) {
        now_ms += POLL_MS;
        bool flickering = nflicker >= FLICKERS &&
                          now_ms - flicker_at[(nflicker - FLICKERS) % FLICKERS] <= FLICKER_SPAN_MS;
        if (flickering || (g.touched && (touched_ms += POLL_MS) >= STUCK_MS)) {
            if (flickering) {
                ESP_LOGW(TAG, "%d short all-zone flickers in %d s: recalibrating the Si12T", FLICKERS,
                         FLICKER_SPAN_MS / 1000);
            } else {
                ESP_LOGW(TAG, "touched for %d s: recalibrating the Si12T", STUCK_MS / 1000);
            }
            nflicker = 0;
            touched_ms = 0;
            esp_err_t err = setup();
            stackchan_head_gesture_reset(&g);
            s_touched = s_firm = false;
            if (s_cb) {
                s_cb(STACKCHAN_HEAD_RELEASE);
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Si12T re-setup failed (%s)", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
            continue;
        }
        if (!g.touched) {
            touched_ms = 0;
        }
        uint8_t out = 0;
        if (rd(SI12T_OUTPUT1, &out) != ESP_OK) {
            if (++errors == MAX_ERRORS) {
                ESP_LOGE(TAG, "Si12T stopped answering; head touch off");
                if (g.touched && s_cb) {
                    s_touched = false;
                    s_cb(STACKCHAN_HEAD_RELEASE);
                }
                vTaskDeleteWithCaps(NULL);
            }
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }
        errors = 0;
        /* Raw zone changes while a hand is near, for tuning the recogniser. */
        static uint8_t last;
        if ((out & 0x3F) != last) {
            ESP_LOGI(TAG, "raw 0x%02x pos %d", out & 0x3F, stackchan_head_position(out));
            last = out & 0x3F;
        }
        static uint8_t firm_streak;
        firm_streak = out & 0x2A ? (firm_streak < 2 ? firm_streak + 1 : 2) : 0;   /* level 2-3 in any zone */
        s_firm = firm_streak >= 2;
        bool was_touched = g.touched;
        stackchan_head_event_t ev = stackchan_head_gesture_update(&g, out);
        if (ev == STACKCHAN_HEAD_PRESS) {
            s_peak = 0;
        }
        if (!was_touched && g.touched) {
            only_settling = true;
        }
        s_touch_ms = g.touched ? touched_ms : 0;
        if (g.touched && (out & 0x2A)) {   /* level 2-3 somewhere: a real touch */
            only_settling = false;
        }
        if (was_touched && !g.touched) {
            if (only_settling && touched_ms <= FLICKER_MS) {
                flicker_at[nflicker++ % FLICKERS] = now_ms;
            } else {
                nflicker = 0;   /* a real touch in between: start counting again */
            }
        }
        if (g.touched) {
            uint8_t z[3];
            stackchan_head_zones(out, z);
            for (int i = 0; i < 3; i++) {
                if (z[i] > s_peak) {
                    s_peak = z[i];
                }
            }
        }
        s_touched = g.touched;
        if (ev != STACKCHAN_HEAD_NONE) {
            ESP_LOGI(TAG, "%s (zones 0x%02x, position %d)",
                     ev == STACKCHAN_HEAD_PRESS ? "press" : ev == STACKCHAN_HEAD_RELEASE ? "release"
                     : ev == STACKCHAN_HEAD_SWIPE_UP ? "swipe 0->2" : "swipe 2->0",
                     out & 0x3F, stackchan_head_position(out));
            if (s_cb) {
                s_cb(ev);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t stackchan_head_start(i2c_master_bus_handle_t bus, stackchan_head_cb_t cb)
{
    ESP_RETURN_ON_FALSE(bus && !s_dev, ESP_ERR_INVALID_STATE, TAG, "bus");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SI12T_ADDR,
        .scl_speed_hz = SI12T_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    esp_err_t err = setup();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Si12T setup failed (%s): head touch off", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    s_cb = cb;
    if (xTaskCreatePinnedToCoreWithCaps(head_task, "head", 3072, NULL, 2, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the head touch task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Si12T head touch ready");
    return ESP_OK;
}

bool stackchan_head_touched(void)
{
    return s_touched;
}

int stackchan_head_touched_ms(void)
{
    return s_touched ? s_touch_ms : 0;
}

bool stackchan_head_firm(void)
{
    return s_firm;
}

int stackchan_head_touch_peak(void)
{
    return s_peak;
}
