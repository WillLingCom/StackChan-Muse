/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#include "stackchan_imu.h"

#include <math.h>
#include <stdio.h>

#include "bmi270.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#define IMU_ADDR    0x69      /* CoreS3 (0x68 is the head's Si12T) */
#define IMU_HZ      50

static const char *TAG = "imu";
static bmi270_handle_t *s_dev;
static bool (*s_hold)(void);
static stackchan_imu_cb_t s_cb;
static motion_t s_m;
static float s_acc[3], s_gyro[3];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_trace_until;

static void imu_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    int64_t last = esp_timer_get_time();
    int errors = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / IMU_HZ));
        float a[3], g[3];
        if (bmi270_get_acce_data(s_dev, &a[0], &a[1], &a[2]) != ESP_OK ||
            bmi270_get_gyro_data(s_dev, &g[0], &g[1], &g[2]) != ESP_OK) {
            if (++errors == 50) {
                ESP_LOGW(TAG, "IMU not answering");
            }
            continue;
        }
        errors = 0;
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f;
        last = now;
        bool hold = s_hold && s_hold();
        taskENTER_CRITICAL(&s_lock);
        for (int i = 0; i < 3; i++) {
            s_acc[i] = a[i];
            s_gyro[i] = g[i];
        }
        float away = sqrtf((a[0] - s_m.g[0]) * (a[0] - s_m.g[0]) + (a[1] - s_m.g[1]) * (a[1] - s_m.g[1]) +
                           (a[2] - s_m.g[2]) * (a[2] - s_m.g[2]));
        uint32_t ev = motion_update(&s_m, a, g, dt, hold);
        motion_t m = s_m;
        taskEXIT_CRITICAL(&s_lock);
        if (now < s_trace_until) {
            static float max_away, max_spin;
            static int n;
            float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
            max_away = away > max_away ? away : max_away;
            max_spin = spin > max_spin ? spin : max_spin;
            if (++n >= IMU_HZ / 10) {
                ESP_LOGI(TAG, "trace away %.2f g spin %.0f dps level %.2f g %.0f dps shaking %d dizzy %.2f hold %d",
                         max_away, max_spin, sqrtf(m.e_acc), sqrtf(m.e_spin), m.shaking, m.dizzy, hold);
                max_away = max_spin = 0;
                n = 0;
            }
        }
        if (ev) {
            ESP_LOGI(TAG, "motion:%s%s%s%s%s dizzy %.2f tilt %.2f", ev & MOTION_SHAKE ? " shake" : "",
                     ev & MOTION_LIFT ? " lift" : "", ev & MOTION_FLIP ? " flip" : "",
                     ev & MOTION_UNFLIP ? " unflip" : "", ev & MOTION_SLIP ? " slip" : "", m.dizzy, m.tilt);
        }
        if (s_cb) {
            s_cb(ev, &m);
        }
    }
}

esp_err_t stackchan_imu_start(i2c_master_bus_handle_t bus, bool (*hold)(void), stackchan_imu_cb_t cb)
{
    bmi270_driver_config_t drv = { .addr = IMU_ADDR, .interface = BMI270_USE_I2C, .i2c_bus = bus };
    esp_err_t err = bmi270_create(&drv, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BMI270 not found (%s): motion play off", esp_err_to_name(err));
        return err;
    }
    bmi270_config_t cfg = {
        .acce_odr = BMI270_ACC_ODR_100_HZ, .acce_range = BMI270_ACC_RANGE_4_G,
        .gyro_odr = BMI270_GYR_ODR_100_HZ, .gyro_range = BMI270_GYR_RANGE_1000_DPS,
    };
    if ((err = bmi270_start(s_dev, &cfg)) != ESP_OK) {
        ESP_LOGW(TAG, "BMI270 start failed (%s)", esp_err_to_name(err));
        return err;
    }
    motion_init(&s_m);
    s_hold = hold;
    s_cb = cb;
    if (xTaskCreatePinnedToCoreWithCaps(imu_task, "imu", 3072, NULL, 2, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "BMI270 ready: motion play on");
    return ESP_OK;
}

void stackchan_imu_trace(int secs)
{
    s_trace_until = esp_timer_get_time() + (int64_t)secs * 1000000;
}

int stackchan_imu_json(char *buf, int cap)
{
    taskENTER_CRITICAL(&s_lock);
    float a[3] = { s_acc[0], s_acc[1], s_acc[2] }, g[3] = { s_gyro[0], s_gyro[1], s_gyro[2] };
    motion_t m = s_m;
    taskEXIT_CRITICAL(&s_lock);
    return snprintf(buf, cap,
                    "{\"ok\":%s,\"acc\":[%.2f,%.2f,%.2f],\"gyro\":[%.0f,%.0f,%.0f],\"up\":[%.2f,%.2f,%.2f],"
                    "\"have_up\":%s,\"tilt\":%.2f,\"dizzy\":%.2f,\"flipped\":%s,\"slipped\":%s}",
                    s_dev ? "true" : "false", a[0], a[1], a[2], g[0], g[1], g[2], m.up[0], m.up[1], m.up[2],
                    m.have_up ? "true" : "false", m.tilt, m.dizzy, m.flipped ? "true" : "false",
                    m.slipped ? "true" : "false");
}
