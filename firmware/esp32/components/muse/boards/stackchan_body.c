/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Register maps and wire protocol follow M5Stack's StackChan firmware
 * (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD):
 * hal/drivers/PY32IOExpander_Class and hal/drivers/FTServo_Arduino (SCSCL).
 */

#include "stackchan_body.h"
#include "stackchan_gesture.h"
#include "stackchan_head.h"
#include "stackchan_py32_regs.h"
#include "stackchan_servo_regs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* PY32 base controller (PY32IOExpander_Class.cpp). */
#define PY32_ADDR       0x6F
#define PY32_HZ         100000
#define PY32_VERSION    0x02
#define PY32_GPIO_M_L   0x03    /* direction, 1 = output */
#define PY32_GPIO_O_L   0x05    /* output latch */
#define PY32_GPIO_I_L   0x07    /* input level */
#define PY32_VM_EN_BIT  0       /* servo power */

/* SCSCL servos on UART1 (hal_servo.cpp, SCSerial.cpp). */
#define SERVO_UART      UART_NUM_1
#define SERVO_TX        6
#define SERVO_RX        7
#define SERVO_BAUD      1000000
#define SCS_READ        0x02
#define SCS_WRITE       0x03    /* only through scs_write's whitelist */
#define REG_TORQUE      SCS_REG_TORQUE
#define REG_GOAL        SCS_REG_GOAL
#define SCS_TIMEOUT_US  10000

static const char *TAG = "body";
static i2c_master_dev_handle_t s_py32;
static bool s_uart;
static SemaphoreHandle_t s_lock;

static esp_err_t py32_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_py32, &reg, 1, val, 1, 20);
}

bool stackchan_py32_read(uint8_t reg, uint8_t *val)
{
    return s_py32 && py32_read(reg, val) == ESP_OK;
}

/* Read-modify-write of one bit. */
static bool py32_bit(uint8_t reg, uint8_t bit, bool on)
{
    uint8_t v;
    if (py32_read(reg, &v) != ESP_OK) {
        return false;
    }
    uint8_t w = on ? (v | bit) : (v & ~bit);
    return w == v || stackchan_py32_write(reg, &w, 1);
}

/* The base's power enable (pin 0), as the factory sets it (StackChan-BSP
 * M5StackChan.cpp, hal_io_expander.cpp): output, pull-up, push-pull, high.
 * Open drain left it floating ("latch high, level low"), the LEDs and IR dark. */
static void base_power_on(void)
{
    if (!s_py32) {
        return;
    }
    bool ok = py32_bit(PY32_REG_GPIO_DRV_L, PY32_VM_PIN_BIT, false) && py32_bit(PY32_REG_GPIO_PD_L, PY32_VM_PIN_BIT, false) &&
              py32_bit(PY32_REG_GPIO_PU_L, PY32_VM_PIN_BIT, true) && py32_bit(PY32_REG_GPIO_O_L, PY32_VM_PIN_BIT, true) &&
              py32_bit(PY32_REG_GPIO_M_L, PY32_VM_PIN_BIT, true);
    uint8_t lvl = 0;
    py32_read(PY32_GPIO_I_L, &lvl);
    ESP_LOGI(TAG, "base power (PY32 pin 0, push-pull high): %s, level %d", ok ? "set" : "FAILED", lvl & 1);
}

bool stackchan_py32_write(uint8_t reg, const uint8_t *data, size_t n)
{
    if (!s_py32 || !stackchan_py32_write_allowed(reg, n)) {
        ESP_LOGE(TAG, "PY32 write refused: reg 0x%02x, %u bytes", reg, (unsigned)n);
        return false;
    }
    uint8_t buf[1 + 2 * PY32_LED_COUNT];
    buf[0] = reg;
    memcpy(buf + 1, data, n);
    return i2c_master_transmit(s_py32, buf, n + 1, 20) == ESP_OK;
}

/* One SCS READ: ff ff id 4 02 addr len sum, answered by ff ff id len+2 err data.. sum. */
static bool scs_read(uint8_t id, uint8_t addr, uint8_t *data, uint8_t len)
{
    uint8_t req[8] = { 0xFF, 0xFF, id, 4, SCS_READ, addr, len, 0 };
    req[7] = (uint8_t)~(id + 4 + SCS_READ + addr + len);
    uart_flush_input(SERVO_UART);
    if (uart_write_bytes(SERVO_UART, req, sizeof(req)) != sizeof(req) ||
        uart_wait_tx_done(SERVO_UART, pdMS_TO_TICKS(5)) != ESP_OK) {
        return false;
    }
    uint8_t rx[6 + 32];
    size_t want = 6u + len, got = 0;
    int64_t until = esp_timer_get_time() + SCS_TIMEOUT_US;
    while (got < want && esp_timer_get_time() < until) {
        int n = uart_read_bytes(SERVO_UART, rx + got, want - got, pdMS_TO_TICKS(2));
        if (n > 0) {
            got += (size_t)n;
        }
    }
    if (got != want || rx[0] != 0xFF || rx[1] != 0xFF || rx[2] != id || rx[3] != len + 2) {
        return false;
    }
    uint8_t sum = 0;
    for (size_t i = 2; i < want - 1; i++) {
        sum += rx[i];
    }
    if ((uint8_t)~sum != rx[want - 1]) {
        return false;
    }
    memcpy(data, rx + 5, len);
    return true;
}

/* One SCS WRITE, confined to registers 40-47, and its status reply. */
static bool scs_write(uint8_t id, uint8_t addr, const uint8_t *data, uint8_t len)
{
    if (!stackchan_servo_write_allowed(addr, len)) {
        ESP_LOGE(TAG, "refused servo write to registers %u..%u", addr, addr + len - 1);
        return false;
    }
    uint8_t req[7 + 6];
    req[0] = req[1] = 0xFF;
    req[2] = id;
    req[3] = (uint8_t)(len + 3);
    req[4] = SCS_WRITE;
    req[5] = addr;
    uint8_t sum = (uint8_t)(id + req[3] + SCS_WRITE + addr);
    for (uint8_t i = 0; i < len; i++) {
        req[6 + i] = data[i];
        sum += data[i];
    }
    req[6 + len] = (uint8_t)~sum;
    uart_flush_input(SERVO_UART);
    size_t n = 7u + len;
    if (uart_write_bytes(SERVO_UART, req, n) != (int)n || uart_wait_tx_done(SERVO_UART, pdMS_TO_TICKS(5)) != ESP_OK) {
        return false;
    }
    uint8_t ack[6];
    size_t got = 0;
    int64_t until = esp_timer_get_time() + SCS_TIMEOUT_US;
    while (got < sizeof(ack) && esp_timer_get_time() < until) {
        int r = uart_read_bytes(SERVO_UART, ack + got, sizeof(ack) - got, pdMS_TO_TICKS(2));
        if (r > 0) {
            got += (size_t)r;
        }
    }
    return got == sizeof(ack) && ack[0] == 0xFF && ack[1] == 0xFF && ack[2] == id && ack[3] == 2 &&
           (uint8_t)~(ack[2] + ack[3] + ack[4]) == ack[5];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);   /* SCSCL words are big-endian (End = 1) */
}

static int16_t sign_mag(uint16_t v, int bit)
{
    return v & (1u << bit) ? -(int16_t)(v & ~(1u << bit)) : (int16_t)v;
}

static void read_servo(uint8_t id, stackchan_servo_status_t *s)
{
    memset(s, 0, sizeof(*s));
    s->id = id;
    uint8_t eep[10], dead[2], torque, fb[15];
    if (!scs_read(id, 3, eep, sizeof(eep)) || !scs_read(id, 26, dead, sizeof(dead)) || !scs_read(id, 40, &torque, 1) ||
        !scs_read(id, 56, fb, sizeof(fb))) {
        return;
    }
    s->cw_dead = dead[0];
    s->ccw_dead = dead[1];
    s->ok = true;
    s->version = be16(eep);              /* 3-4 */
    s->min_limit = be16(eep + 6);        /* 9-10 */
    s->max_limit = be16(eep + 8);        /* 11-12 */
    s->torque = torque != 0;
    s->position = be16(fb);              /* 56-57 */
    s->speed = sign_mag(be16(fb + 2), 15);
    s->load = sign_mag(be16(fb + 4), 10);
    s->voltage_dv = fb[6];               /* 62 */
    s->temperature_c = fb[7];            /* 63 */
    s->moving = fb[10] != 0;             /* 66 */
    s->current = sign_mag(be16(fb + 13), 15);   /* 69-70 */
}

esp_err_t stackchan_body_start(i2c_master_bus_handle_t bus)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "lock");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PY32_ADDR,
        .scl_speed_hz = PY32_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_py32);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PY32: %s", esp_err_to_name(err));
        s_py32 = NULL;
    }
    const uart_config_t uc = {
        .baud_rate = SERVO_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(SERVO_UART, 256, 0, 0, NULL, 0);
    if (err == ESP_OK) {
        err = uart_param_config(SERVO_UART, &uc);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(SERVO_UART, SERVO_TX, SERVO_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "servo UART: %s", esp_err_to_name(err));
        uart_driver_delete(SERVO_UART);
    } else {
        s_uart = true;
    }
    base_power_on();
    ESP_LOGI(TAG, "body diagnostics ready (read-only): PY32 %s, servo bus %s", s_py32 ? "added" : "missing",
             s_uart ? "up" : "down");
    return ESP_OK;
}

void stackchan_body_read(stackchan_body_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t m = 0, o = 0, i = 0;
    if (s_py32 && py32_read(PY32_VERSION, &out->py32_version) == ESP_OK && py32_read(PY32_GPIO_M_L, &m) == ESP_OK &&
        py32_read(PY32_GPIO_O_L, &o) == ESP_OK && py32_read(PY32_GPIO_I_L, &i) == ESP_OK) {
        out->py32_ok = out->py32_version != 0 && out->py32_version != 0xFF;
        out->vm_output = m & (1u << PY32_VM_EN_BIT);
        out->vm_level = o & (1u << PY32_VM_EN_BIT);
        out->vm_input = i & (1u << PY32_VM_EN_BIT);
    }
    if (s_uart) {
        read_servo(STACKCHAN_YAW_ID, &out->servo[0]);
        read_servo(STACKCHAN_PITCH_ID, &out->servo[1]);
    }
    xSemaphoreGive(s_lock);
}

/* 0.3125 degrees a step (hal_servo.cpp: raw = zero + angle * 16 / 5 / 10). */
static float degrees(const stackchan_servo_status_t *s, int zero)
{
    return ((int)s->position - zero) * 0.3125f;
}

int stackchan_body_json(const stackchan_body_status_t *st, char *buf, size_t cap)
{
    static const char *const NAMES[2] = { "yaw", "pitch" };
    const int zeros[2] = { CONFIG_MUSE_STACKCHAN_YAW_ZERO, CONFIG_MUSE_STACKCHAN_PITCH_ZERO };
    int n = snprintf(buf, cap, "{\"py32\":{\"ok\":%s,\"version\":%u,\"vm_en\":{\"output\":%s,\"latch\":%s,\"level\":%s}},"
                     "\"servos\":[", st->py32_ok ? "true" : "false", st->py32_version, st->vm_output ? "true" : "false",
                     st->vm_level ? "true" : "false", st->vm_input ? "true" : "false");
    for (int k = 0; k < 2 && n > 0 && (size_t)n < cap; k++) {
        const stackchan_servo_status_t *s = &st->servo[k];
        if (!s->ok) {
            n += snprintf(buf + n, cap - n, "%s{\"axis\":\"%s\",\"id\":%u,\"ok\":false}", k ? "," : "", NAMES[k], s->id);
            continue;
        }
        n += snprintf(buf + n, cap - n,
                      "%s{\"axis\":\"%s\",\"id\":%u,\"ok\":true,\"version\":%u,\"limits\":[%u,%u],\"dead\":[%u,%u],\"torque\":%s,"
                      "\"raw\":%u,\"zero\":%d,\"deg\":%.1f,\"speed\":%d,\"load\":%d,\"volt\":%.1f,\"temp_c\":%u,"
                      "\"moving\":%s,\"current\":%d}",
                      k ? "," : "", NAMES[k], s->id, s->version, s->min_limit, s->max_limit, s->cw_dead, s->ccw_dead, s->torque ? "true" : "false",
                      s->position, zeros[k], degrees(s, zeros[k]), s->speed, s->load, s->voltage_dv / 10.0f,
                      s->temperature_c, s->moving ? "true" : "false", s->current);
    }
    if (n > 0 && (size_t)n < cap) {
        n += snprintf(buf + n, cap - n, "]}");
    }
    return n;
}

/* ---- S3 phase 2a: one supervised nudge ---- */

#define NUDGE_MOVE_MS   1000
#define NUDGE_WAIT_MS   800     /* after the ramp, to settle before giving up */
#define NUDGE_STEP_MS   20      /* goal updates at 50 Hz */
#define NUDGE_FB_EVERY  3       /* read feedback every 3rd step (60 ms) */
#define NUDGE_HOLD_MS   500
#define NUDGE_NEAR      10      /* ticks (3.1 deg, user approved). Measured: these servos settle
                                 * 5-8 ticks short of the goal although their dead band is 1;
                                 * M5Stack's stall check treats only 8+ ticks as far */
#define NUDGE_MAX_LOAD  600     /* 0..1000; M5Stack's stall check uses 650 */
#define NUDGE_MAX_CUR   300     /* raw; M5Stack's uses 350 */
#define NUDGE_MIN_DV    45
#define NUDGE_MAX_DV    56
#define NUDGE_MAX_TEMP  50
#define NUDGE_SAMPLES   48

static volatile bool s_stop, s_moving;
static volatile int64_t s_stop_us;   /* when the stop was asked for */

typedef struct {
    int axis, dir;
    stackchan_body_result_cb_t cb;
} nudge_req_t;

typedef struct {
    int16_t t, pos;
} sample_t;

static bool set_torque(uint8_t id, bool on)
{
    uint8_t v = on;
    for (int i = 0; i < 3; i++) {
        if (scs_write(id, REG_TORQUE, &v, 1)) {
            return true;
        }
    }
    return false;
}

static bool set_goal(uint8_t id, uint16_t pos, uint16_t ms)
{
    uint8_t g[6] = { (uint8_t)(pos >> 8), (uint8_t)pos, (uint8_t)(ms >> 8), (uint8_t)ms, 0, 0 };
    /* Idempotent: one lost reply (seen once in S7) shouldn't abort a move. */
    return scs_write(id, REG_GOAL, g, sizeof(g)) || scs_write(id, REG_GOAL, g, sizeof(g));
}

static bool feedback(uint8_t id, int *pos, int *load, int *cur, bool *moving)
{
    uint8_t fb[15];
    /* A read changes nothing: one lost reply (seen once in a dance's first glide) is retried. */
    if (!scs_read(id, 56, fb, sizeof(fb)) && !scs_read(id, 56, fb, sizeof(fb))) {
        return false;
    }
    *pos = be16(fb);
    *load = sign_mag(be16(fb + 4), 10);
    *moving = fb[10] != 0;
    *cur = sign_mag(be16(fb + 13), 15);
    return true;
}

/* Moves from `from` to target along a smooth ramp over NUDGE_MOVE_MS, a new
 * goal every NUDGE_STEP_MS with a 20 ms move time (M5Stack's hal_servo.cpp
 * writes WritePos(id, pos, 20, 0) at 50 Hz the same way; one goal with a
 * long move time stalled short on this servo), then waits for it to settle.
 * Returns NULL, or why it stopped. */
static const char *move_to(uint8_t id, int from, int target, int64_t t0, sample_t *log, int *nlog, int *max_load,
                           int *max_cur, int *reached_ms, int *last)
{
    *last = -1;
    int64_t start = esp_timer_get_time();
    int step = 0;
    for (;;) {
        int elapsed = (int)((esp_timer_get_time() - start) / 1000);
        if (elapsed > NUDGE_MOVE_MS + NUDGE_WAIT_MS) {
            return "target not reached in time";
        }
        if (s_stop) {
            return "stop requested";
        }
        if (stackchan_head_touched()) {
            return "hand on the head";
        }
        float u = elapsed >= NUDGE_MOVE_MS ? 1.0f : (float)elapsed / NUDGE_MOVE_MS;
        float ease = u * u * (3.0f - 2.0f * u);   /* smoothstep: no jerk at either end */
        int goal = from + (int)lroundf((target - from) * ease);
        if (!set_goal(id, (uint16_t)goal, NUDGE_STEP_MS)) {
            return "goal write failed";
        }
        if (step++ % NUDGE_FB_EVERY == 0 || u >= 1.0f) {
            int pos, load, cur;
            bool moving;
            if (!feedback(id, &pos, &load, &cur, &moving)) {
                return "no feedback";
            }
            if (abs(load) > *max_load) {
                *max_load = abs(load);
            }
            if (abs(cur) > *max_cur) {
                *max_cur = abs(cur);
            }
            if (*nlog < NUDGE_SAMPLES) {
                log[(*nlog)++] = (sample_t){ (int16_t)((esp_timer_get_time() - t0) / 1000), (int16_t)pos };
            }
            if (abs(load) > NUDGE_MAX_LOAD || abs(cur) > NUDGE_MAX_CUR) {
                return "load or current over limit";
            }
            /* Settled: stopped, the same position twice, near the goal and at
             * least half way there from where it started. */
            bool halfway = (target - from) * (pos - from) * 2 >= (target - from) * (target - from);
            if (u >= 1.0f && !moving && pos == *last && abs(pos - target) <= NUDGE_NEAR && halfway) {
                *reached_ms = elapsed;
                return NULL;
            }
            *last = pos;
        }
        vTaskDelay(pdMS_TO_TICKS(NUDGE_STEP_MS));
    }
}

static void nudge_task(void *arg)
{
    nudge_req_t req = *(nudge_req_t *)arg;
    free(arg);
    const uint8_t id = req.axis ? STACKCHAN_PITCH_ID : STACKCHAN_YAW_ID;
    static sample_t log[NUDGE_SAMPLES];
    int nlog = 0, max_load = 0, max_cur = 0, reach1 = -1, reach2 = -1, start = -1, target = -1, end = -1;
    int at1 = -1, at2 = -1;   /* where each leg settled */
    const char *why = NULL;
    bool torque_on = false;
    int64_t t0 = esp_timer_get_time();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    stackchan_servo_status_t st;
    read_servo(id, &st);
    if (!st.ok) {
        why = "refused: servo not answering";
    } else if (st.voltage_dv < NUDGE_MIN_DV || st.voltage_dv > NUDGE_MAX_DV) {
        why = "refused: supply voltage out of range";
    } else if (st.temperature_c > NUDGE_MAX_TEMP) {
        why = "refused: servo too hot";
    } else if (st.torque) {
        why = "refused: torque already on";
    } else {
        start = st.position;
        target = start + req.dir * STACKCHAN_NUDGE_TICKS;
        if (target < st.min_limit + 30 || target > st.max_limit - 30) {
            why = "refused: target near the servo's limits";
        }
    }
    if (!why) {
        /* Goal first, so torque holds where it is instead of jumping. */
        if (!set_goal(id, (uint16_t)start, 0)) {
            why = "goal write failed";
        } else {
            torque_on = true;   /* from here on, always try to turn it off: a lost reply isn't a lost write */
            if (!set_torque(id, true)) {
                why = "torque-on failed";
            }
        }
    }
    if (!why) {
        why = move_to(id, start, target, t0, log, &nlog, &max_load, &max_cur, &reach1, &at1);
    }
    if (!why) {
        vTaskDelay(pdMS_TO_TICKS(NUDGE_HOLD_MS));
        why = s_stop ? "stop requested" : move_to(id, target, start, t0, log, &nlog, &max_load, &max_cur, &reach2, &at2);
    }
    if (torque_on) {
        int pos, load, cur;
        bool moving;
        if (why && feedback(id, &pos, &load, &cur, &moving)) {
            set_goal(id, (uint16_t)pos, 0);   /* hold here before letting go */
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        if (!set_torque(id, false)) {
            why = why ? why : "torque-off failed";
            ESP_LOGE(TAG, "torque-off failed on servo %u", id);
        }
    }
    int pos, load, cur;
    bool moving;
    if (feedback(id, &pos, &load, &cur, &moving)) {
        end = pos;
    }
    xSemaphoreGive(s_lock);

    char json[160 + NUDGE_SAMPLES * 14];
    int n = snprintf(json, sizeof(json),
                     "{\"axis\":\"%s\",\"dir\":%d,\"result\":\"%s\",\"start\":%d,\"target\":%d,\"end\":%d,"
                     "\"reach_ms\":[%d,%d],\"settled\":[%d,%d],\"max_load\":%d,\"max_current\":%d,\"samples\":[",
                     req.axis ? "pitch" : "yaw", req.dir, why ? why : "ok", start, target, end, reach1, reach2, at1, at2,
                     max_load, max_cur);
    for (int i = 0; i < nlog && n > 0 && (size_t)n < sizeof(json) - 16; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s[%d,%d]", i ? "," : "", log[i].t, log[i].pos);
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    ESP_LOGI(TAG, "nudge: %s", json);
    if (req.cb) {
        req.cb(json, NULL);
    }
    s_moving = false;
    vTaskDeleteWithCaps(NULL);
}

bool stackchan_body_nudge(int axis, int dir, stackchan_body_result_cb_t cb)
{
    if (!s_uart || (axis != 0 && axis != 1) || (dir != 1 && dir != -1) || (axis == 1 && dir != 1)) {
        return false;   /* pitch goes up only, away from the stop it rests on */
    }
    if (__atomic_exchange_n(&s_moving, true, __ATOMIC_ACQ_REL)) {
        return false;
    }
    nudge_req_t *req = malloc(sizeof(*req));
    if (!req) {
        s_moving = false;
        return false;
    }
    *req = (nudge_req_t){ axis, dir, cb };
    s_stop = false;
    if (xTaskCreatePinnedToCoreWithCaps(nudge_task, "nudge", 4096, req, 3, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        free(req);
        s_moving = false;
        return false;
    }
    return true;
}

void stackchan_body_stop(void)
{
    if (s_moving && !s_stop) {
        s_stop_us = esp_timer_get_time();
        s_stop = true;
    }
}

bool stackchan_body_moving(void)
{
    return s_moving;
}

/* ---- S3 phase 2b: gestures on both axes ---- */

#define G_STEP_MS        20
#define G_FB_EVERY       3
#define G_MAX_LAG        45      /* ticks between goal and position before "not following" */
#define G_SETTLE_MS      800
#define G_HOLD_MS        2000    /* after a stop, before lowering */
#define G_TOUCH_WAIT_MS  10000   /* hold while a hand stays on the head, at most */
#define G_LOWER_TICKS_S  64      /* 20 deg/s */
#define G_PAUSE_MAX_MS   30000   /* a hand that never leaves: lower (into it) anyway */
#define DEG_TICKS        3.2f    /* 1 / 0.3125 */
#define REST_GUESS       (CONFIG_MUSE_STACKCHAN_PITCH_ZERO - 8)   /* measured rests: 595-603 with zero 610 */

typedef struct {
    int gesture, repeat;
    float speed;
    stackchan_body_result_cb_t cb;
    void *ctx;
} gesture_req_t;

typedef struct {
    float goal[2];          /* commanded raw, yaw and pitch */
    int pos[2];
    int max_load[2], max_lag;
} axes_t;

static const uint8_t IDS[2] = { STACKCHAN_YAW_ID, STACKCHAN_PITCH_ID };

static bool write_goals(axes_t *a)
{
    return set_goal(IDS[0], (uint16_t)lroundf(a->goal[0]), G_STEP_MS) &&
           set_goal(IDS[1], (uint16_t)lroundf(a->goal[1]), G_STEP_MS);
}

/* Reads both, tracks the worst load and lag; NULL or why to stop. */
static const char *check_axes(axes_t *a, bool *settled)
{
    bool stopped = true;
    for (int k = 0; k < 2; k++) {
        int pos, load, cur;
        bool moving;
        if (!feedback(IDS[k], &pos, &load, &cur, &moving)) {
            return "no feedback";
        }
        a->pos[k] = pos;
        if (abs(load) > a->max_load[k]) {
            a->max_load[k] = abs(load);
        }
        int lag = abs(pos - (int)lroundf(a->goal[k]));
        if (lag > a->max_lag) {
            a->max_lag = lag;
        }
        if (abs(load) > NUDGE_MAX_LOAD) {
            return "load over limit";
        }
        if (lag > G_MAX_LAG) {
            return "not following";
        }
        stopped &= !moving && lag <= NUDGE_NEAR;
    }
    if (settled) {
        *settled = stopped;
    }
    return NULL;
}

/* Glides both axes to (y, p) raw over ms with smoothstep easing. */
static const char *glide(axes_t *a, float y, float p, int ms)
{
    float y0 = a->goal[0], p0 = a->goal[1];
    int64_t start = esp_timer_get_time();
    for (int step = 0;; step++) {
        if (s_stop) {
            return "stop requested";
        }
        if (stackchan_head_touched()) {
            return "hand on the head";
        }
        int t = (int)((esp_timer_get_time() - start) / 1000);
        float u = t >= ms ? 1.0f : (float)t / ms;
        float e = u * u * (3.0f - 2.0f * u);
        a->goal[0] = y0 + (y - y0) * e;
        a->goal[1] = p0 + (p - p0) * e;
        if (!write_goals(a)) {
            return "goal write failed";
        }
        if (step % G_FB_EVERY == 0) {
            const char *why = check_axes(a, NULL);
            if (why) {
                return why;
            }
        }
        if (u >= 1.0f) {
            return NULL;
        }
        vTaskDelay(pdMS_TO_TICKS(G_STEP_MS));
    }
}

/* After a stop or fault: hold, then lower the pitch onto its rest slowly. */
static void safe_down(axes_t *a, float rest, bool touched)
{
    for (int k = 0; k < 2; k++) {   /* hold where it actually is */
        int pos, load, cur;
        bool moving;
        if (feedback(IDS[k], &pos, &load, &cur, &moving)) {
            a->goal[k] = (float)pos;
        }
    }
    write_goals(a);
    int64_t until = esp_timer_get_time() + (int64_t)(touched ? G_TOUCH_WAIT_MS : G_HOLD_MS) * 1000;
    while (esp_timer_get_time() < until && (!touched || stackchan_head_touched())) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    int paused_ms = 0;
    while (a->goal[1] > rest) {
        if (stackchan_head_touched() && paused_ms < G_PAUSE_MAX_MS) {   /* pause under a hand */
            vTaskDelay(pdMS_TO_TICKS(50));
            paused_ms += 50;
            continue;
        }
        a->goal[1] -= G_LOWER_TICKS_S * G_STEP_MS / 1000.0f;
        if (a->goal[1] < rest) {
            a->goal[1] = rest;
        }
        write_goals(a);
        vTaskDelay(pdMS_TO_TICKS(G_STEP_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(300));
}

/* With the bus held: both servos healthy and no hand on the head. A limp
 * head sits on its lower stop: that's the rest a move returns to. */
static const char *preflight(stackchan_servo_status_t st[2], float *rest)
{
    read_servo(IDS[0], &st[0]);
    read_servo(IDS[1], &st[1]);
    for (int k = 0; k < 2; k++) {
        if (!st[k].ok) {
            return "refused: servo not answering";
        } else if (st[k].voltage_dv < NUDGE_MIN_DV || st[k].voltage_dv > NUDGE_MAX_DV) {
            return "refused: supply voltage out of range";
        } else if (st[k].temperature_c > NUDGE_MAX_TEMP) {
            return "refused: servo too hot";
        }
    }
    if (stackchan_head_touched()) {
        return "refused: hand on the head";
    }
    if (!st[1].torque && st[1].position <= CONFIG_MUSE_STACKCHAN_PITCH_ZERO + 16) {
        *rest = st[1].position;
    }
    return NULL;
}

static void gesture_task(void *arg)
{
    gesture_req_t req = *(gesture_req_t *)arg;
    free(arg);
    axes_t a = { 0 };
    gesture_plan_t plan;
    const char *why = NULL, *plan_why = NULL;
    bool torque_on = false, settled = false;
    int64_t t0 = esp_timer_get_time();
    float rest = REST_GUESS;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    stackchan_servo_status_t st[2];
    why = preflight(st, &rest);
    if (!why) {
        float yaw_now = (st[0].position - CONFIG_MUSE_STACKCHAN_YAW_ZERO) / DEG_TICKS;
        float pitch_now = (st[1].position - rest) / DEG_TICKS;
        plan_why = gesture_plan((gesture_id_t)req.gesture, req.repeat, req.speed, yaw_now, pitch_now, &plan);
        if (plan_why) {
            why = "refused: plan";
        }
    }
    if (!why) {
        a.goal[0] = st[0].position;
        a.goal[1] = st[1].position;
        torque_on = true;   /* from here on, always end with torque off */
        if (!write_goals(&a) || !set_torque(IDS[0], true) || !set_torque(IDS[1], true)) {
            why = "torque-on failed";
        }
    }
    for (int i = 0; !why && i < plan.n; i++) {
        const gesture_frame_t *f = &plan.frame[i];
        why = glide(&a, CONFIG_MUSE_STACKCHAN_YAW_ZERO + f->yaw * DEG_TICKS, rest + f->pitch * DEG_TICKS, f->ms);
    }
    if (!why) {
        int64_t until = esp_timer_get_time() + G_SETTLE_MS * 1000;
        while (!why && !settled && esp_timer_get_time() < until) {
            vTaskDelay(pdMS_TO_TICKS(60));
            why = check_axes(&a, &settled);
        }
    }
    bool aborted = why && torque_on;
    int64_t abort_us = esp_timer_get_time();
    int stop_at = s_stop ? (int)((s_stop_us - t0) / 1000) : -1;
    int stop_lag = s_stop ? (int)((abort_us - s_stop_us) / 1000) : -1;
    if (aborted) {
        /* A touch also calls stackchan_body_stop(): go by the hand, not the reason. */
        safe_down(&a, rest, stackchan_head_touched() || !strcmp(why, "hand on the head"));
    }
    if (torque_on && (!set_torque(IDS[0], false) || !set_torque(IDS[1], false))) {
        ESP_LOGE(TAG, "torque-off failed after a gesture");
        why = why ? why : "torque-off failed";
    }
    int end[2] = { -1, -1 };
    for (int k = 0; k < 2; k++) {
        int pos, load, cur;
        bool moving;
        if (feedback(IDS[k], &pos, &load, &cur, &moving)) {
            end[k] = pos;
        }
    }
    xSemaphoreGive(s_lock);

    char json[384];
    snprintf(json, sizeof(json),
             "{\"gesture\":\"%s\",\"repeat\":%d,\"speed\":%.2f,\"result\":\"%s%s%s\",\"settled\":%s,"
             "\"planned_ms\":%u,\"ms\":%d,\"rest\":%.0f,\"end\":[%d,%d],\"max_load\":[%d,%d],\"max_lag\":%d,"
             "\"stop_at_ms\":%d,\"stop_to_hold_ms\":%d}",
             gesture_name((gesture_id_t)req.gesture), req.repeat, req.speed, why ? why : "ok", plan_why ? ": " : "",
             plan_why ? plan_why : "", settled ? "true" : "false", plan_why ? 0u : (unsigned)plan.total_ms,
             (int)((esp_timer_get_time() - t0) / 1000), rest, end[0], end[1], a.max_load[0], a.max_load[1], a.max_lag,
             stop_at, stop_lag);
    ESP_LOGI(TAG, "gesture: %s", json);
    s_moving = false;   /* before the callback, so it may start the next one */
    if (req.cb) {
        req.cb(json, req.ctx);
    }
    vTaskDeleteWithCaps(NULL);
}

bool stackchan_body_gesture(int gesture, int repeat, float speed, stackchan_body_result_cb_t cb, void *ctx)
{
    if (!s_uart || gesture < 0 || gesture >= GESTURE_COUNT) {
        return false;
    }
    if (__atomic_exchange_n(&s_moving, true, __ATOMIC_ACQ_REL)) {
        return false;
    }
    gesture_req_t *req = malloc(sizeof(*req));
    if (!req) {
        s_moving = false;
        return false;
    }
    *req = (gesture_req_t){ gesture, repeat, speed, cb, ctx };
    s_stop = false;
    if (xTaskCreatePinnedToCoreWithCaps(gesture_task, "gesture", 4096, req, 3, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        free(req);
        s_moving = false;
        return false;
    }
    return true;
}

/* ---- F8: following a moving target (a dance) ---- */

#define F_FIRST_MS  700     /* glide to the first pose */
#define F_TOUCH_MS  300     /* a level-1-only touch must last this long to stop a dance: brief
                             * all-zone level-1 blips come and go with nobody touching (a person
                             * close by); a firm touch, or the screen, stops at once */
#define F_HOME_MS   800

typedef struct {
    stackchan_follow_pose_t pose;
    void (*ready)(void *ctx);
    uint32_t max_ms;
    stackchan_body_result_cb_t cb;
    void *ctx;
} follow_req_t;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void follow_task(void *arg)
{
    follow_req_t req = *(follow_req_t *)arg;
    free(arg);
    axes_t a = { 0 };
    const char *why = NULL, *pose_why = NULL;
    bool torque_on = false, settled = false;
    int64_t t0 = esp_timer_get_time();
    float rest = REST_GUESS, y = 0, p = 0;
    int steps = 0, capped = 0;
    const float yaw0 = CONFIG_MUSE_STACKCHAN_YAW_ZERO;
    /* Per-step speed caps: the executor's own limits, whatever the pose asks. */
    const float max_dy = GESTURE_YAW_SPEED * G_STEP_MS / 1000.0f * DEG_TICKS;
    const float max_dp = GESTURE_PITCH_SPEED * G_STEP_MS / 1000.0f * DEG_TICKS;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    stackchan_servo_status_t st[2];
    why = preflight(st, &rest);
    if (!why && req.pose(req.ctx, &y, &p, &pose_why) != 1) {
        why = pose_why ? pose_why : "refused: nothing to follow";
    }
    if (!why) {
        a.goal[0] = st[0].position;
        a.goal[1] = st[1].position;
        torque_on = true;   /* from here on, always end with torque off */
        if (!write_goals(&a) || !set_torque(IDS[0], true) || !set_torque(IDS[1], true)) {
            why = "torque-on failed";
        }
    }
    if (!why) {
        why = glide(&a, yaw0 + clampf(y, -GESTURE_YAW_LIMIT, GESTURE_YAW_LIMIT) * DEG_TICKS,
                    rest + clampf(p, 0, GESTURE_PITCH_LIMIT) * DEG_TICKS, F_FIRST_MS);
    }
    if (!why && req.ready) {
        req.ready(req.ctx);
    }
    TickType_t wake = xTaskGetTickCount();
    while (!why) {
        if (s_stop) {
            why = "stop requested";
            break;
        }
        if (stackchan_head_touched() && (stackchan_head_firm() || stackchan_head_touched_ms() >= F_TOUCH_MS)) {
            why = "hand on the head";
            break;
        }
        if ((esp_timer_get_time() - t0) / 1000 > req.max_ms) {
            why = "longer than allowed";
            break;
        }
        int rc = req.pose(req.ctx, &y, &p, &pose_why);
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            why = pose_why ? pose_why : "aborted";
            break;
        }
        float g[2] = { yaw0 + clampf(y, -GESTURE_YAW_LIMIT, GESTURE_YAW_LIMIT) * DEG_TICKS,
                       rest + clampf(p, 0, GESTURE_PITCH_LIMIT) * DEG_TICKS };
        const float cap[2] = { max_dy, max_dp };
        for (int k = 0; k < 2; k++) {
            float d = g[k] - a.goal[k];
            if (fabsf(d) > cap[k]) {
                g[k] = a.goal[k] + copysignf(cap[k], d);
                capped++;
            }
            a.goal[k] = g[k];
        }
        if (!write_goals(&a)) {
            why = "goal write failed";
        } else if (steps++ % G_FB_EVERY == 0) {
            why = check_axes(&a, NULL);
        }
        if (!why) {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(G_STEP_MS));
        }
    }
    if (!why) {
        why = glide(&a, yaw0, rest, F_HOME_MS);
    }
    if (!why) {
        int64_t until = esp_timer_get_time() + G_SETTLE_MS * 1000;
        while (!why && !settled && esp_timer_get_time() < until) {
            vTaskDelay(pdMS_TO_TICKS(60));
            why = check_axes(&a, &settled);
        }
    }
    int64_t abort_us = esp_timer_get_time();
    int stop_lag = s_stop ? (int)((abort_us - s_stop_us) / 1000) : -1;
    if (why && torque_on) {
        safe_down(&a, rest, stackchan_head_touched() || !strcmp(why, "hand on the head"));
    }
    if (torque_on && (!set_torque(IDS[0], false) || !set_torque(IDS[1], false))) {
        ESP_LOGE(TAG, "torque-off failed after following");
        why = why ? why : "torque-off failed";
    }
    int end[2] = { -1, -1 };
    for (int k = 0; k < 2; k++) {
        int pos, load, cur;
        bool moving;
        if (feedback(IDS[k], &pos, &load, &cur, &moving)) {
            end[k] = pos;
        }
    }
    xSemaphoreGive(s_lock);

    char json[320];
    snprintf(json, sizeof(json),
             "{\"result\":\"%s\",\"settled\":%s,\"ms\":%d,\"steps\":%d,\"speed_capped\":%d,\"rest\":%.0f,"
             "\"end\":[%d,%d],\"max_load\":[%d,%d],\"max_lag\":%d,\"stop_to_hold_ms\":%d}",
             why ? why : "ok", settled ? "true" : "false", (int)((esp_timer_get_time() - t0) / 1000), steps, capped,
             rest, end[0], end[1], a.max_load[0], a.max_load[1], a.max_lag, stop_lag);
    ESP_LOGI(TAG, "follow: %s", json);
    s_moving = false;
    if (req.cb) {
        req.cb(json, req.ctx);
    }
    vTaskDeleteWithCaps(NULL);
}

bool stackchan_body_follow(stackchan_follow_pose_t pose, void (*ready)(void *ctx), uint32_t max_ms,
                           stackchan_body_result_cb_t cb, void *ctx)
{
    if (!s_uart || !pose) {
        return false;
    }
    if (__atomic_exchange_n(&s_moving, true, __ATOMIC_ACQ_REL)) {
        return false;
    }
    follow_req_t *req = malloc(sizeof(*req));
    if (!req) {
        s_moving = false;
        return false;
    }
    *req = (follow_req_t){ pose, ready, max_ms, cb, ctx };
    s_stop = false;
    /* Above the voice task's priority (6), so the head keeps its 50 Hz while music renders. */
    if (xTaskCreatePinnedToCoreWithCaps(follow_task, "follow", 4096, req, 7, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        free(req);
        s_moving = false;
        return false;
    }
    return true;
}
