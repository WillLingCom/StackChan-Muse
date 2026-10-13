/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

#include "stackchan_mic.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"

static const char *TAG = "mic";

typedef struct {
    int16_t *buf;
    size_t want, got;   /* stereo frames */
} rec_t;

static void tap(const int16_t *stereo, size_t frames, void *ctx)
{
    rec_t *r = ctx;
    size_t n = r->want - r->got < frames ? r->want - r->got : frames;
    memcpy(r->buf + 2 * r->got, stereo, n * 2 * sizeof(int16_t));
    r->got += n;
}

int16_t *stackchan_mic_record(int seconds, size_t *frames)
{
    if (seconds < 1 || seconds > STACKCHAN_MIC_MAX_S) {
        return NULL;
    }
    static rec_t r;
    r.want = (size_t)MUSE_AUDIO_RATE * seconds;
    r.got = 0;
    r.buf = heap_caps_malloc(r.want * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!r.buf) {
        return NULL;
    }
    muse_audio_set_tap(tap, &r);
    for (int waited = 0; r.got < r.want && waited < (seconds + 2) * 1000; waited += 50) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    muse_audio_set_tap(NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(30));   /* a chunk in flight finishes before the buffer is handed out */
    if (r.got < r.want) {
        ESP_LOGW(TAG, "only %u of %u frames arrived", (unsigned)r.got, (unsigned)r.want);
        if (!r.got) {
            heap_caps_free(r.buf);
            return NULL;
        }
    }
    *frames = r.got;
    return r.buf;
}
