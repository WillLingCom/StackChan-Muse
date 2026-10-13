/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The Si12T register sequence and gesture thresholds follow M5Stack's StackChan
 * firmware (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD).
 */

/*
 * StackChan K151 head touch (Si12T at 0x68 on the CoreS3's internal I2C bus).
 * A low-priority task polls the chip every 50 ms and reports debounced
 * gestures (stackchan_head_gesture.h) to the callback, on that task.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "stackchan_head_gesture.h"

typedef void (*stackchan_head_cb_t)(stackchan_head_event_t ev);

/* Sets the chip up as M5Stack's firmware does and starts polling. A missing
 * or silent chip returns an error and leaves the rest of the board running. */
esp_err_t stackchan_head_start(i2c_master_bus_handle_t bus, stackchan_head_cb_t cb);
/* True while a hand is on the head (debounced). */
bool stackchan_head_touched(void);
/* True while some zone reads level 2+ in the last two samples. Not used to
 * stop motion: in the e-stop test (sway, a hand on the head) the readings
 * stayed at level 1 while the servos ran, so motion stops on any debounced
 * touch (stackchan_head_touched), accepting false stops over missed ones. */
/* How long the current touch has lasted, ms (0 when untouched). */
int stackchan_head_touched_ms(void);
bool stackchan_head_firm(void);
/* The strongest zone level (0-3) seen during the current or last touch. */
int stackchan_head_touch_peak(void);
