/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Pins from M5Stack's StackChan-BSP (@8d4d6fc, MIT): examples/IR/Send (LED 5),
 * examples/IR/Receive (receiver 10).
 */
#include "stackchan_ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"

#define IR_TX_GPIO      5
#define IR_RX_GPIO      10
#define IR_RES_HZ       1000000                 /* 1 tick = 1 us */
#define IR_TX_SYMBOLS   48
#define IR_RX_SYMBOLS   192                     /* all four RX blocks: up to 383 edges */
#define IR_GLITCH_NS    1000
#define IR_IDLE_NS      20000000                /* 20 ms quiet ends a frame */
#define IR_DUTY         0.33f   /* the usual; 1/2 and full pad drive brought no extra reach */
#define IR_NS           "k151_ir"
#define IR_KEEP_ENTRIES 128                     /* NVS entries (32 B) always left free for Muse */

static const char *TAG = "ir";
static rmt_channel_handle_t s_tx, s_rx;
static rmt_encoder_handle_t s_copy;
static QueueHandle_t s_rx_q;
static volatile bool s_busy;
static rmt_symbol_word_t s_rx_syms[IR_RX_SYMBOLS];
static rmt_symbol_word_t s_tx_syms[IR_MAX_REPEAT * ((IR_MAX_EDGES + 1) / 2)];

static bool IRAM_ATTR on_rx(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *e, void *ctx)
{
    (void)ch;
    (void)ctx;
    BaseType_t woke = pdFALSE;
    xQueueSendFromISR(s_rx_q, e, &woke);
    return woke == pdTRUE;
}

static const char *setup(void)
{
    if (s_tx && s_rx) {
        return NULL;
    }
    rmt_tx_channel_config_t tx = {
        .gpio_num = IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RES_HZ,
        .mem_block_symbols = IR_TX_SYMBOLS,
        .trans_queue_depth = 2,
    };
    rmt_rx_channel_config_t rx = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RES_HZ,
        .mem_block_symbols = IR_RX_SYMBOLS,
    };
    rmt_copy_encoder_config_t copy = {};
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx };
    s_rx_q = s_rx_q ? s_rx_q : xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    esp_err_t err;
    if (!s_rx_q) {
        return "no memory";
    }
    if ((err = rmt_new_tx_channel(&tx, &s_tx)) != ESP_OK || (err = rmt_new_copy_encoder(&copy, &s_copy)) != ESP_OK ||
        (err = rmt_enable(s_tx)) != ESP_OK || (err = rmt_new_rx_channel(&rx, &s_rx)) != ESP_OK ||
        (err = rmt_rx_register_event_callbacks(s_rx, &cbs, NULL)) != ESP_OK || (err = rmt_enable(s_rx)) != ESP_OK) {
        ESP_LOGE(TAG, "setup failed: %s", esp_err_to_name(err));
        return "infrared setup failed";
    }
    ESP_LOGI(TAG, "ready: TX GPIO%d, RX GPIO%d", IR_TX_GPIO, IR_RX_GPIO);
    return NULL;
}

/* One radio operation at a time; a second one is refused, not queued. */
static bool lock(void)
{
    return !__atomic_exchange_n(&s_busy, true, __ATOMIC_ACQ_REL);
}

static void unlock(void)
{
    __atomic_store_n(&s_busy, false, __ATOMIC_RELEASE);
}

/* Arms one reception. */
static bool arm(void)
{
    rmt_receive_config_t cfg = { .signal_range_min_ns = IR_GLITCH_NS, .signal_range_max_ns = IR_IDLE_NS };
    xQueueReset(s_rx_q);
    return rmt_receive(s_rx, s_rx_syms, sizeof(s_rx_syms), &cfg) == ESP_OK;
}

/* Gives up a pending reception. */
static void disarm(void)
{
    rmt_disable(s_rx);
    rmt_enable(s_rx);
}

/* Symbols to mark/space durations: the receiver idles high, so level 0 is a mark. */
static size_t to_edges(const rmt_rx_done_event_data_t *e, uint16_t *us, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < e->num_symbols; i++) {
        const rmt_symbol_word_t *s = &e->received_symbols[i];
        uint32_t lv[2] = { s->level0, s->level1 }, d[2] = { s->duration0, s->duration1 };
        for (int k = 0; k < 2; k++) {
            if (!d[k]) {
                return n;   /* the end */
            }
            if (n == 0 && lv[k] != 0) {
                continue;   /* start on the first mark */
            }
            if (n < cap) {
                us[n++] = (uint16_t)(d[k] > 65535 ? 65535 : d[k]);
            }
        }
    }
    return n;
}

const char *stackchan_ir_learn(int timeout_ms, ir_code_t *out)
{
    if (!lock()) {
        return "busy: another infrared operation is running";
    }
    const char *why = setup();
    static uint16_t us[IR_RX_SYMBOLS * 2];
    int64_t until = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int noise = 0;
    while (!why) {
        if (!arm()) {
            why = "receiver failed";
            break;
        }
        int64_t left = (until - esp_timer_get_time()) / 1000;
        rmt_rx_done_event_data_t e;
        if (left <= 0 || xQueueReceive(s_rx_q, &e, pdMS_TO_TICKS(left)) != pdTRUE) {
            disarm();
            why = noise ? "only noise heard: press the remote's button again, pointed at the robot"
                        : "no remote button heard in time";
            break;
        }
        size_t n = to_edges(&e, us, sizeof(us) / sizeof(us[0]));
        if (n < 6) {
            noise++;   /* a flicker of light, not a remote */
            continue;
        }
        size_t first = ir_first_frame(us, n);
        uint32_t frame_us = 0;
        for (size_t i = 0; i < first; i++) {
            frame_us += us[i];
        }
        ESP_LOGI(TAG, "capture: %u edges, first frame %u edges / %lu us, gap after %u us, lead %u/%u us",
                 (unsigned)n, (unsigned)first, (unsigned long)frame_us, first < n ? us[first] : 0, us[0],
                 n > 1 ? us[1] : 0);
        why = ir_code_from_capture(us, n, out);
        ESP_LOGI(TAG, "learned: %u edges -> %s", (unsigned)n,
                 why ? why : out->proto == IR_PROTO_NEC ? "NEC" : "raw");
        break;
    }
    unlock();
    return why;
}

static const char *send_locked(const ir_code_t *c)
{
    const char *why = ir_code_check(c);
    if (why) {
        return why;
    }
    /* The frame `repeat` times, the gap as the last space of each but the last. */
    size_t n = 0;
    for (int r = 0; r < c->repeat; r++) {
        bool last = r == c->repeat - 1;
        for (size_t i = 0; i < c->n; i += 2, n++) {
            s_tx_syms[n] = (rmt_symbol_word_t){
                .level0 = 1, .duration0 = c->us[i],
                .level1 = 0, .duration1 = i + 1 < c->n ? c->us[i + 1] : last ? 100 : c->gap_us,
            };
        }
    }
    rmt_carrier_config_t carrier = { .frequency_hz = c->carrier_hz, .duty_cycle = IR_DUTY };
    rmt_transmit_config_t cfg = { .loop_count = 0 };
    if (rmt_apply_carrier(s_tx, &carrier) != ESP_OK ||
        rmt_transmit(s_tx, s_copy, s_tx_syms, n * sizeof(s_tx_syms[0]), &cfg) != ESP_OK ||
        rmt_tx_wait_all_done(s_tx, 1000) != ESP_OK) {
        return "transmit failed";
    }
    return NULL;
}

const char *stackchan_ir_send(const ir_code_t *c)
{
    if (!lock()) {
        return "busy: another infrared operation is running";
    }
    const char *why = setup();
    if (!why) {
        why = send_locked(c);
    }
    ESP_LOGI(TAG, "send %s, %u edges x%u (gap %u us): %s", c->proto == IR_PROTO_NEC ? "NEC" : "raw", c->n,
             c->repeat, c->gap_us, why ? why : "sent");
    unlock();
    return why;
}

/* ---- self-test ---- */

/* A 3 ms + 3 ms header and 8 marks: no remote protocol uses it. */
static void test_pattern(ir_code_t *c)
{
    static const uint16_t P[] = { 3000, 3000, 500, 700, 500, 1400, 500, 700, 500, 1400,
                                  500, 1400, 500, 700, 500, 1400, 500 };
    memset(c, 0, sizeof(*c));
    c->proto = IR_PROTO_RAW;
    c->carrier_hz = IR_CARRIER_HZ;
    c->repeat = 1;
    c->n = sizeof(P) / sizeof(P[0]);
    memcpy(c->us, P, sizeof(P));
}

const char *stackchan_ir_selftest(int repeats, char *json, size_t cap)
{
    if (repeats < 1 || repeats > 30) {
        return "repeats must be 1-30";
    }
    if (!lock()) {
        return "busy: another infrared operation is running";
    }
    const char *why = setup();
    ir_code_t c;
    test_pattern(&c);
    int sent = 0, heard = 0, matched = 0;
    static uint16_t us[IR_RX_SYMBOLS * 2];
    for (int i = 0; !why && i < repeats; i++) {
        bool armed = arm();
        if (send_locked(&c)) {
            why = "transmit failed";
            break;
        }
        sent++;
        rmt_rx_done_event_data_t e;
        if (armed && xQueueReceive(s_rx_q, &e, pdMS_TO_TICKS(80)) == pdTRUE) {
            heard++;
            size_t n = to_edges(&e, us, sizeof(us) / sizeof(us[0]));
            bool ok = n == c.n;
            for (size_t k = 0; ok && k < n; k++) {
                ok = us[k] > c.us[k] * 6 / 10 && us[k] < c.us[k] * 14 / 10;
            }
            matched += ok;
        } else if (armed) {
            disarm();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!why) {
        snprintf(json, cap, "{\"sent\":%d,\"heard_by_own_receiver\":%d,\"matched\":%d}", sent, heard, matched);
        ESP_LOGI(TAG, "self-test: %s", json);
    }
    unlock();
    return why;
}

const char *stackchan_ir_echo(const char *name, char *json, size_t cap)
{
    static ir_code_t c;
    const char *why = stackchan_ir_load(name, &c);
    if (why) {
        return why;
    }
    if (!lock()) {
        return "busy: another infrared operation is running";
    }
    why = setup();
    static uint16_t us[IR_RX_SYMBOLS * 2];
    size_t n = 0;
    bool armed = !why && arm();
    if (!why && send_locked(&c)) {
        why = "transmit failed";
    }
    rmt_rx_done_event_data_t e;
    if (!why && armed && xQueueReceive(s_rx_q, &e, pdMS_TO_TICKS(200)) == pdTRUE) {
        n = to_edges(&e, us, sizeof(us) / sizeof(us[0]));
    } else if (armed) {
        disarm();
    }
    if (!why) {
        size_t first = ir_first_frame(us, n);
        int worst = 0, sum_mark = 0, sum_space = 0, marks = 0, spaces = 0;
        for (size_t i = 0; i < first && i < c.n; i++) {
            int d = (int)us[i] - (int)c.us[i];
            worst = abs(d) > abs(worst) ? d : worst;
            if (i & 1) {
                sum_space += d;
                spaces++;
            } else {
                sum_mark += d;
                marks++;
            }
        }
        int k = snprintf(json, cap, "{\"name\":\"%s\",\"sent_edges\":%u,\"x\":%u,\"heard_edges\":%u,"
                         "\"heard_first_frame\":%u,\"worst_us\":%d,\"mean_mark_err_us\":%d,"
                         "\"mean_space_err_us\":%d,\"heard\":[", name, c.n, c.repeat, (unsigned)n,
                         (unsigned)first, worst, marks ? sum_mark / marks : 0, spaces ? sum_space / spaces : 0);
        for (size_t i = 0; i < first && i < 24 && k > 0 && (size_t)k < cap - 16; i++) {
            k += snprintf(json + k, cap - k, "%s%u/%u", i ? "," : "", us[i], i < c.n ? c.us[i] : 0);
        }
        if (k > 0 && (size_t)k < cap - 2) {
            snprintf(json + k, cap - k, "]}");
        }
        ESP_LOGI(TAG, "echo: %s", json);
    }
    unlock();
    return why;
}

const char *stackchan_ir_padcheck(char *json, size_t cap)
{
    if (!lock()) {
        return "busy: another infrared operation is running";
    }
    const char *why = setup();
    if (!why) {
        gpio_input_enable(IR_TX_GPIO);   /* read the pad back; output stays on RMT */
        int idle = gpio_get_level(IR_TX_GPIO);
        static ir_code_t c;
        memset(&c, 0, sizeof(c));
        c.proto = IR_PROTO_RAW;
        c.carrier_hz = IR_CARRIER_HZ;
        c.repeat = 1;
        c.n = 3;
        c.us[0] = 20000; c.us[1] = 1000; c.us[2] = 20000;   /* two 20 ms carrier marks */
        rmt_carrier_config_t carrier = { .frequency_hz = c.carrier_hz, .duty_cycle = IR_DUTY };
        rmt_transmit_config_t cfg = { .loop_count = 0 };
        s_tx_syms[0] = (rmt_symbol_word_t){ .level0 = 1, .duration0 = 20000, .level1 = 0, .duration1 = 1000 };
        s_tx_syms[1] = (rmt_symbol_word_t){ .level0 = 1, .duration0 = 20000, .level1 = 0, .duration1 = 100 };
        int hi = 0, lo = 0, n = 0;
        if (rmt_apply_carrier(s_tx, &carrier) == ESP_OK &&
            rmt_transmit(s_tx, s_copy, s_tx_syms, 2 * sizeof(s_tx_syms[0]), &cfg) == ESP_OK) {
            int64_t end = esp_timer_get_time() + 30000;   /* sample during the first mark */
            while (esp_timer_get_time() < end) {
                gpio_get_level(IR_TX_GPIO) ? hi++ : lo++;
                n++;
            }
            rmt_tx_wait_all_done(s_tx, 1000);
        } else {
            why = "transmit failed";
        }
        int after = gpio_get_level(IR_TX_GPIO);
        if (!why) {
            snprintf(json, cap, "{\"idle\":%d,\"highs\":%d,\"lows\":%d,\"samples\":%d,\"after\":%d}", idle, hi, lo,
                     n, after);
            ESP_LOGI(TAG, "pad check: %s", json);
        }
    }
    unlock();
    return why;
}

bool stackchan_ir_busy(void)
{
    return s_busy;
}

/* ---- storage: keys c0..c9, each the name (32 bytes) then the packed code ---- */

#define SLOT_BYTES (IR_NAME_MAX + 1 + IR_PACK_MAX)

/* Buffers come from the heap: these run on callers' small stacks (a console
 * or link task overflowed on an ir.list with them on the stack). */
static bool read_slot(nvs_handle_t h, int i, char *name, ir_code_t *c)
{
    uint8_t *buf = malloc(SLOT_BYTES);
    if (!buf) {
        return false;
    }
    char key[16];
    snprintf(key, sizeof(key), "c%d", i);
    size_t len = SLOT_BYTES;
    bool ok = nvs_get_blob(h, key, buf, &len) == ESP_OK && len >= IR_NAME_MAX + 1 + 10;
    if (ok) {
        memcpy(name, buf, IR_NAME_MAX + 1);
        name[IR_NAME_MAX] = 0;
        ok = !c || ir_code_unpack(buf + IR_NAME_MAX + 1, len - IR_NAME_MAX - 1, c);
    }
    free(buf);
    return ok;
}

/* The slot holding `name`, or -1; *free_slot the first empty one (or -1). */
static int find(nvs_handle_t h, const char *name, int *free_slot)
{
    char n[IR_NAME_MAX + 1];
    int found = -1;
    *free_slot = -1;
    for (int i = 0; i < IR_MAX_CODES; i++) {
        if (!read_slot(h, i, n, NULL)) {
            if (*free_slot < 0) {
                *free_slot = i;
            }
        } else if (found < 0 && !strcmp(n, name)) {
            found = i;
        }
    }
    return found;
}

const char *stackchan_ir_save(const char *name, const ir_code_t *c)
{
    if (!ir_name_ok(name)) {
        return "the name must be 1-31 bytes, no control characters";
    }
    uint8_t *buf = calloc(1, SLOT_BYTES);
    if (!buf) {
        return "no memory";
    }
    memcpy(buf, name, strlen(name));
    size_t len = ir_code_pack(c, buf + IR_NAME_MAX + 1, IR_PACK_MAX);
    if (!len) {
        free(buf);
        return "not a valid code";
    }
    len += IR_NAME_MAX + 1;
    nvs_handle_t h;
    if (nvs_open(IR_NS, NVS_READWRITE, &h) != ESP_OK) {
        free(buf);
        return "storage unavailable";
    }
    const char *why = NULL;
    int free_slot, slot = find(h, name, &free_slot);
    nvs_stats_t st;
    size_t need = (len + 31) / 32 + 2;
    if (slot < 0 && free_slot < 0) {
        why = "already 10 codes saved: delete one first";
    } else if (nvs_get_stats(NULL, &st) != ESP_OK || st.free_entries < need + IR_KEEP_ENTRIES) {
        why = "storage nearly full: not saving (Muse's own data comes first)";
    } else {
        char key[16];
        snprintf(key, sizeof(key), "c%d", slot >= 0 ? slot : free_slot);
        if (nvs_set_blob(h, key, buf, len) != ESP_OK || nvs_commit(h) != ESP_OK) {
            why = "could not save";
        }
    }
    nvs_close(h);
    free(buf);
    ESP_LOGI(TAG, "save \"%s\" (%u B): %s", name, (unsigned)len, why ? why : "ok");
    return why;
}

const char *stackchan_ir_load(const char *name, ir_code_t *out)
{
    nvs_handle_t h;
    if (!ir_name_ok(name)) {
        return "no such code";
    }
    if (nvs_open(IR_NS, NVS_READONLY, &h) != ESP_OK) {
        return "no such code";
    }
    int free_slot, slot = find(h, name, &free_slot);
    char n[IR_NAME_MAX + 1];
    bool ok = slot >= 0 && read_slot(h, slot, n, out);
    nvs_close(h);
    return slot < 0 ? "no such code" : ok ? NULL : "the saved code is damaged";
}

const char *stackchan_ir_delete(const char *name)
{
    nvs_handle_t h;
    if (!ir_name_ok(name) || nvs_open(IR_NS, NVS_READWRITE, &h) != ESP_OK) {
        return "no such code";
    }
    int free_slot, slot = find(h, name, &free_slot);
    const char *why = NULL;
    if (slot < 0) {
        why = "no such code";
    } else {
        char key[16];
        snprintf(key, sizeof(key), "c%d", slot);
        if (nvs_erase_key(h, key) != ESP_OK || nvs_commit(h) != ESP_OK) {
            why = "could not delete";
        }
    }
    nvs_close(h);
    return why;
}

int stackchan_ir_list(char names[][IR_NAME_MAX + 1], uint8_t *protos, int max)
{
    nvs_handle_t h;
    if (nvs_open(IR_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    int n = 0;
    ir_code_t *c = malloc(sizeof(*c));
    for (int i = 0; c && i < IR_MAX_CODES && n < max; i++) {
        if (read_slot(h, i, names[n], c)) {
            protos[n++] = c->proto;
        }
    }
    free(c);
    nvs_close(h);
    return n;
}
