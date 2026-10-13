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
 * Push-to-talk turn loop: hold -> stream speech to Hatch, release -> think -> speak.
 * Out of Hatch's reach (Wi-Fi down, say) a note is saved to PSRAM and goes once
 * it's back, on boards with PSRAM. Consumes muse_input_event_t from `queue` and
 * drives muse_state for the UI.
 */
esp_err_t muse_voice_start(QueueHandle_t queue);

/*
 * Hands-free turn events (CONFIG_MUSE_HANDS_FREE), on the voice task: START
 * when the user's speech begins, END when it ends (the note goes to Muse),
 * CANCEL when a turn is dropped unsent, SESSION_END when the conversation is
 * over. Keep the listener short. NULL removes it.
 */
typedef enum {
    MUSE_VOICE_TURN_START,
    MUSE_VOICE_TURN_END,
    MUSE_VOICE_TURN_CANCEL,
    MUSE_VOICE_SESSION_END,
} muse_voice_turn_event_t;
void muse_voice_set_turn_listener(void (*cb)(muse_voice_turn_event_t ev));

/*
 * A turn the device starts by itself once the current one has finished (at
 * once if idle): the message is `text` (copied) plus any JPEG attached with
 * muse_hatch_attach_jpeg, and the reply is shown and spoken like any other.
 * done(true) when Muse answered, done(false) if the turn failed; on the voice
 * task. A newer request replaces a pending one. Safe from any task.
 */
void muse_voice_request_followup(const char *text, void (*done)(bool ok));

/*
 * A board's audio job (a dance, say), run on the voice task once the current
 * turn is over (at once if idle), so it never plays over a reply. It plays
 * with muse_audio_write and returns soon after interrupted() turns true (a
 * press is waiting; it stays queued). A newer request replaces a pending one.
 * Safe from any task.
 */
typedef void (*muse_voice_job_t)(bool (*interrupted)(void));
void muse_voice_request_job(muse_voice_job_t job);

/* While on (settings' Sound page), idle mic audio feeds muse_voice_monitor_db(). */
void muse_voice_set_monitor(bool on);
/* Smoothed mic level in dBFS (fast attack, slow release). */
float muse_voice_monitor_db(void);

/* Plays a short chirp at the current volume (when idle). */
void muse_voice_request_chirp(void);

/* Runs muse_audio_loopback_test() at the current volume (when idle); results go to the log. */
void muse_voice_request_loopback(void);

/* Bench test: decodes and plays a built-in MP3 reply. */
void muse_voice_request_mp3test(void);

/* Asleep with nothing to play: codecs off, Wi-Fi dozing. */
bool muse_voice_resting(void);

/* Voice notes recorded out of Hatch's reach wait to go, the oldest from the
 * last half hour: worth keeping Wi-Fi up for. */
bool muse_voice_notes_waiting(void);
