/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * The head's BMI270 IMU (CoreS3, I2C 0x69), polled at 50 Hz into the motion
 * recogniser (stackchan_motion.h). The callback runs on the IMU task with the
 * events that began and the live state; keep it short.
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "stackchan_motion.h"

typedef void (*stackchan_imu_cb_t)(uint32_t events, const motion_t *m);
/* hold(): true while the head's own motion (servos) or a busy state should be ignored. */
esp_err_t stackchan_imu_start(i2c_master_bus_handle_t bus, bool (*hold)(void), stackchan_imu_cb_t cb);
/* The latest raw sample (g, deg/s) and recogniser state as JSON, for checking axes. */
int stackchan_imu_json(char *buf, int cap);
/* For tuning: log every 100 ms, for `secs`, the peak swing away from gravity
 * (g), the peak spin (deg/s) and the shake recogniser's jerk count. */
void stackchan_imu_trace(int secs);
