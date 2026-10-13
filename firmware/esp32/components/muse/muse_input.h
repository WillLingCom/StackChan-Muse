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

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Buttons, as the board reports them. The talk button is push-to-talk. On touch
 * boards the aux button puts the screen to sleep; holding it 1.5 s powers off,
 * and two quick presses toggle BLE phone setup. Without touch it opens the
 * two-button menu instead (muse_menu.h), where talk selects. Either button
 * wakes from sleep; the talk button's press is also posted, marked `wake`, so
 * holding it on through waking records a note. Waking also retries Wi-Fi at
 * once if it's down. Also runs auto-sleep and refreshes battery status into
 * muse_state.
 */

typedef enum {
    MUSE_PTT_DOWN,
    MUSE_PTT_UP,
} muse_ptt_t;

typedef struct {
    muse_ptt_t type;
    bool wake;   /* a press that woke the screen: a tap only wakes */
} muse_input_event_t;

/* PTT events are posted to `queue` (items are muse_input_event_t). */
esp_err_t muse_input_start(QueueHandle_t queue);

/*
 * Touch push-to-talk (a long press on the character): behaves like the talk
 * button's press and release, read on the input task's next poll. Ignored
 * unless Muse is online, so a touch never confirms a pairing; only the
 * physical talk button does that.
 */
void muse_input_touch_talk(bool down);

/*
 * Hands-free talk (muse_settings_hands_free): a tap on the character or a
 * touch on the head counts as a talk press with no release. It starts a
 * conversation turn, or interrupts Muse while it answers; the turn ends when
 * the user stops talking. Ignored unless the setting is on and Muse is online.
 * Safe from any task.
 */
void muse_input_hands_free_tap(void);

/* Hands-free taps do nothing for the next `ms` (a tap that only stopped something, e.g. a dance). */
void muse_input_hold_taps(int ms);

/* Wakes the screen (backlight and all) and restarts its idle count, as a press would; for prompts
 * the user must see. */
void muse_input_wake(const char *why);

/* Plays the goodbye animation and powers off (from the input task). */
void muse_input_request_power_off(void);
