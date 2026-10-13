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
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spoken replies through MiniMax's T2A v2 (api.minimaxi.com), with
 * CONFIG_MUSE_TTS_MINIMAX. Off until an API key is set, in the build
 * (CONFIG_MUSE_TTS_MINIMAX_KEY) or with the "tts.key=" setup command, which is
 * kept in NVS and wins; "tts.voice=" picks the voice the same way. A worker
 * task fetches each reply as MP3 and the chat session reads it from here
 * without blocking, as it arrives. Built without it, replies are text only.
 */

typedef enum {
    MUSE_TTS_IDLE,
    MUSE_TTS_RUNNING,    /* fetching; more may arrive */
    MUSE_TTS_DONE,       /* all of it is in the buffer */
    MUSE_TTS_FAILED,
} muse_tts_state_t;

#if CONFIG_MUSE_TTS_MINIMAX
/* A key is set. */
bool muse_tts_enabled(void);
/* "" turns speech off. */
esp_err_t muse_tts_set_key(const char *key);
/* "" goes back to the default voice. */
esp_err_t muse_tts_set_voice(const char *voice);

/* Starts fetching text's speech, ending any fetch before it. False if off or the worker can't start. */
bool muse_tts_start(const char *text);
/* Up to cap bytes of MP3 that have arrived; 0 if none yet. Doesn't block. */
size_t muse_tts_read(uint8_t *buf, size_t cap);
muse_tts_state_t muse_tts_state(void);
/* Abandons the fetch in progress, if any. */
void muse_tts_cancel(void);
#else
static inline bool muse_tts_enabled(void) { return false; }
static inline esp_err_t muse_tts_set_key(const char *key) { (void)key; return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t muse_tts_set_voice(const char *voice) { (void)voice; return ESP_ERR_NOT_SUPPORTED; }
static inline bool muse_tts_start(const char *text) { (void)text; return false; }
static inline size_t muse_tts_read(uint8_t *buf, size_t cap) { (void)buf; (void)cap; return 0; }
static inline muse_tts_state_t muse_tts_state(void) { return MUSE_TTS_IDLE; }
static inline void muse_tts_cancel(void) {}
#endif

#ifdef __cplusplus
}
#endif
