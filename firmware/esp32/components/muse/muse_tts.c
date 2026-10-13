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
 * MiniMax T2A v2: POST https://api.minimaxi.com/v1/t2a_v2 with
 * "Authorization: Bearer <key>". The reply is JSON whose data.audio holds the
 * MP3, hex-encoded; it's decoded straight out of the HTTP stream into a buffer
 * the chat session drains, so speech starts before the whole reply is in.
 * Same request as the rorolee app's MiniMaxTtsService, but MP3 at 16 kHz to
 * keep it small (the session's minimp3 decodes it).
 */
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "muse_tts.h"

static const char *TAG = "muse_tts";

#define URL "https://api.minimaxi.com/v1/t2a_v2"
#define MODEL CONFIG_MUSE_TTS_MINIMAX_MODEL
#define DEFAULT_VOICE CONFIG_MUSE_TTS_MINIMAX_VOICE
#define VOLUME 1.4      /* the app's level for the device speaker */
#define NVS_NS "muse_tts"
#define KEY_MAX 1024     /* MiniMax keys can be long JWTs */
#define VOICE_MAX 96
#define TEXT_CAP 1100
#define MP3_RING (64 * 1024)
#define CHUNK 512

static char s_key[KEY_MAX], s_voice[VOICE_MAX];
static bool s_loaded;
static TaskHandle_t s_task;
static StreamBufferHandle_t s_mp3;
static char *s_text;
static volatile muse_tts_state_t s_state = MUSE_TTS_IDLE;
static volatile bool s_busy, s_cancel;

/* What a setup command saved wins; the build's settings fill in the rest. */
static void load(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    bool key_saved = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_key);
        key_saved = nvs_get_str(h, "key", s_key, &n) == ESP_OK;
        n = sizeof(s_voice);
        nvs_get_str(h, "voice", s_voice, &n);
        nvs_close(h);
    }
    if (!key_saved) {
        strlcpy(s_key, CONFIG_MUSE_TTS_MINIMAX_KEY, sizeof(s_key));
    }
}

static esp_err_t save(const char *name, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    /* An empty key is saved too: off on purpose, not back to the build's key. */
    err = value[0] || !strcmp(name, "key") ? nvs_set_str(h, name, value) : nvs_erase_key(h, name);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool muse_tts_enabled(void)
{
    load();
    return s_key[0] != '\0';
}

esp_err_t muse_tts_set_key(const char *key)
{
    if (strlen(key) >= KEY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    load();
    strlcpy(s_key, key, sizeof(s_key));
    ESP_LOGI(TAG, "spoken replies %s", key[0] ? "on (MiniMax)" : "off");
    return save("key", key);
}

esp_err_t muse_tts_set_voice(const char *voice)
{
    if (strlen(voice) >= VOICE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    load();
    strlcpy(s_voice, voice, sizeof(s_voice));
    ESP_LOGI(TAG, "voice %s", voice[0] ? voice : DEFAULT_VOICE);
    return save("voice", voice);
}

/* Queues n bytes for the session, waiting while the buffer is full. False if cancelled. */
static bool push(const uint8_t *buf, size_t n)
{
    while (n && !s_cancel) {
        size_t sent = xStreamBufferSend(s_mp3, buf, n, pdMS_TO_TICKS(100));
        buf += sent;
        n -= sent;
    }
    return !s_cancel;
}

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static char *request_body(const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", MODEL);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddBoolToObject(root, "stream", false);
    cJSON *voice = cJSON_AddObjectToObject(root, "voice_setting");
    cJSON_AddStringToObject(voice, "voice_id", s_voice[0] ? s_voice : DEFAULT_VOICE);
    cJSON_AddNumberToObject(voice, "speed", 1.0);
    cJSON_AddNumberToObject(voice, "vol", VOLUME);
    cJSON_AddNumberToObject(voice, "pitch", 0);
    cJSON *audio = cJSON_AddObjectToObject(root, "audio_setting");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "bitrate", 32000);
    cJSON_AddStringToObject(audio, "format", "mp3");
    cJSON_AddNumberToObject(audio, "channel", 1);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Fetches s_text's speech into s_mp3. True if any audio came. */
static bool fetch(void)
{
    char *body = request_body(s_text);
    if (!body) {
        return false;
    }
    char auth[KEY_MAX + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);
    const esp_http_client_config_t cfg = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    bool ok = false;
    size_t audio_bytes = 0;
    char head[160] = "";   /* the reply's start, for the log when there's no audio */
    size_t head_len = 0;
    if (!c) {
        goto out;
    }
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    const int len = strlen(body);
    if (esp_http_client_open(c, len) != ESP_OK || esp_http_client_write(c, body, len) != len) {
        ESP_LOGW(TAG, "can't reach MiniMax");
        goto out;
    }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);

    /* Scan for "audio", then ':' and '"', then decode hex pairs up to the closing quote. */
    static const char KEY[] = "\"audio\"";
    enum { FIND, COLON, QUOTE, HEX, AFTER } st = FIND;
    size_t matched = 0;
    int hi = -1;
    uint8_t out[CHUNK];
    size_t out_len = 0;
    char buf[1024];
    int n;
    while (!s_cancel && (n = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (head_len < sizeof(head) - 1 && st != HEX) {
                head[head_len++] = ch;
            }
            switch (st) {
            case FIND:
                matched = ch == KEY[matched] ? matched + 1 : ch == KEY[0] ? 1 : 0;
                if (matched == sizeof(KEY) - 1) {
                    st = COLON;
                }
                break;
            case COLON:
                if (ch == ':') {
                    st = QUOTE;
                } else if (ch != ' ') {
                    st = FIND;
                    matched = 0;
                }
                break;
            case QUOTE:
                if (ch == '"') {
                    st = HEX;
                } else if (ch != ' ') {
                    st = FIND;   /* "audio": null, say */
                    matched = 0;
                }
                break;
            case HEX: {
                if (ch == '"') {
                    st = AFTER;
                    break;
                }
                int v = hex(ch);
                if (v < 0) {
                    break;
                }
                if (hi < 0) {
                    hi = v;
                    break;
                }
                out[out_len++] = (uint8_t)(hi << 4 | v);
                hi = -1;
                if (out_len == sizeof(out)) {
                    if (!push(out, out_len)) {
                        goto out;
                    }
                    audio_bytes += out_len;
                    out_len = 0;
                }
                break;
            }
            case AFTER:
                break;
            }
        }
    }
    if (out_len && push(out, out_len)) {
        audio_bytes += out_len;
    }
    head[head_len] = '\0';
    ok = audio_bytes > 0 && !s_cancel;
    if (!ok && !s_cancel) {
        ESP_LOGW(TAG, "no speech: HTTP %d, %s", status, head);
    } else if (ok) {
        ESP_LOGI(TAG, "speech: %u bytes of MP3", (unsigned)audio_bytes);
    }
out:
    if (c) {
        esp_http_client_cleanup(c);
    }
    cJSON_free(body);
    return ok;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool ok = fetch();
        s_state = s_cancel ? MUSE_TTS_IDLE : ok ? MUSE_TTS_DONE : MUSE_TTS_FAILED;
        s_busy = false;
    }
}

bool muse_tts_start(const char *text)
{
    if (!muse_tts_enabled() || !text || !text[0]) {
        return false;
    }
    if (!s_task) {
        /* PSRAM for the ring and the stack: TLS and the HTTP client run on it. */
        uint8_t *ring = heap_caps_malloc(MP3_RING + 1, MALLOC_CAP_SPIRAM);
        StaticStreamBuffer_t *sb = heap_caps_malloc(sizeof(*sb), MALLOC_CAP_SPIRAM);
        s_text = heap_caps_malloc(TEXT_CAP, MALLOC_CAP_SPIRAM);
        if (!ring || !sb || !s_text) {
            return false;
        }
        s_mp3 = xStreamBufferCreateStatic(MP3_RING, 1, ring, sb);
        if (xTaskCreatePinnedToCoreWithCaps(worker, "muse_tts", 8192, NULL, 4, &s_task, tskNO_AFFINITY,
                                            MALLOC_CAP_SPIRAM) != pdPASS) {
            s_task = NULL;
            return false;
        }
    }
    muse_tts_cancel();
    for (int i = 0; s_busy && i < 300; i++) {   /* the last fetch winding down */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_busy) {
        return false;
    }
    xStreamBufferReset(s_mp3);
    /* Cut a long reply at a character boundary, never inside a UTF-8 sequence:
     * MiniMax rejects the request if its JSON isn't valid UTF-8. */
    size_t n = strnlen(text, TEXT_CAP - 1);
    while (n && n < strlen(text) && ((uint8_t)text[n] & 0xC0) == 0x80) {
        n--;
    }
    memcpy(s_text, text, n);
    s_text[n] = '\0';
    s_cancel = false;
    s_state = MUSE_TTS_RUNNING;
    s_busy = true;
    xTaskNotifyGive(s_task);
    return true;
}

size_t muse_tts_read(uint8_t *buf, size_t cap)
{
    return s_mp3 ? xStreamBufferReceive(s_mp3, buf, cap, 0) : 0;
}

muse_tts_state_t muse_tts_state(void)
{
    /* Done means done once the session has read it all. */
    if (s_state == MUSE_TTS_DONE && s_mp3 && !xStreamBufferIsEmpty(s_mp3)) {
        return MUSE_TTS_RUNNING;
    }
    return s_state;
}

void muse_tts_cancel(void)
{
    if (s_busy) {
        s_cancel = true;
    }
    s_state = MUSE_TTS_IDLE;
}
