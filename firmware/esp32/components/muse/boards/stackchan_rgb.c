/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The LED setup follows M5Stack's StackChan firmware (m5stack/StackChan @1b57655, MIT):
 * hal/hal_io_expander.cpp and hal/drivers/PY32IOExpander_Class.
 */
#include "stackchan_rgb.h"

#include "esp_log.h"
#include "stackchan_body.h"
#include "stackchan_py32_regs.h"

static const char *TAG = "rgb";
static bool s_ready;

/* Read-modify-write of pin 13's bit in a high-byte config register. */
static bool set_pin_bit(uint8_t reg, bool on)
{
    uint8_t v;
    if (!stackchan_py32_read(reg, &v)) {
        return false;
    }
    uint8_t w = on ? (v | PY32_LED_PIN_BIT) : (v & ~PY32_LED_PIN_BIT);
    return w == v || stackchan_py32_write(reg, &w, 1);
}

static bool setup(void)
{
    if (s_ready) {
        return true;
    }
    uint8_t count = PY32_LED_COUNT;
    /* Output, pull-up, push-pull, 12 LEDs (hal_io_expander.cpp). */
    s_ready = set_pin_bit(PY32_REG_GPIO_M_H, true) && set_pin_bit(PY32_REG_GPIO_PD_H, false) &&
              set_pin_bit(PY32_REG_GPIO_PU_H, true) && set_pin_bit(PY32_REG_GPIO_DRV_H, false) &&
              stackchan_py32_write(PY32_REG_LED_CFG, &count, 1);
    ESP_LOGI(TAG, "LED setup %s", s_ready ? "done" : "failed");
    return s_ready;
}

static uint16_t rgb565(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r = r > STACKCHAN_RGB_MAX ? STACKCHAN_RGB_MAX : r;
    g = g > STACKCHAN_RGB_MAX ? STACKCHAN_RGB_MAX : g;
    b = b > STACKCHAN_RGB_MAX ? STACKCHAN_RGB_MAX : b;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool stackchan_rgb_set(uint32_t left, uint32_t right)
{
    if (!setup()) {
        return false;
    }
    uint8_t ram[2 * PY32_LED_COUNT];
    for (int i = 0; i < PY32_LED_COUNT; i++) {
        uint16_t v = rgb565(i < PY32_LED_COUNT / 2 ? left : right);
        ram[2 * i] = (uint8_t)v;
        ram[2 * i + 1] = (uint8_t)(v >> 8);
    }
    uint8_t cfg;
    if (!stackchan_py32_write(PY32_REG_LED_RAM, ram, sizeof(ram)) || !stackchan_py32_read(PY32_REG_LED_CFG, &cfg)) {
        return false;
    }
    if ((cfg & 0x3F) != PY32_LED_COUNT) {   /* the base restarted and lost its LED setup: redo it */
        s_ready = false;
        if (!setup() || !stackchan_py32_write(PY32_REG_LED_RAM, ram, sizeof(ram)) ||
            !stackchan_py32_read(PY32_REG_LED_CFG, &cfg)) {
            return false;
        }
    }
    cfg |= PY32_LED_REFRESH;
    return stackchan_py32_write(PY32_REG_LED_CFG, &cfg, 1);
}

void stackchan_rgb_off(void)
{
    if (s_ready) {
        stackchan_rgb_set(0, 0);
    }
}
