/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * The PY32 base controller (0x6F) registers this port may write: only what the
 * factory firmware writes (hal_io_expander.cpp, PY32IOExpander_Class.cpp,
 * StackChan-BSP M5StackChan.cpp): pin 0, the base's power enable (direction,
 * output, pulls, drive: read-modify-write of bit 0 of the low bytes), pin 13
 * for the 12 RGB LEDs (the same, bit 5 of the high bytes), the LED config and
 * the LED RAM. All are RAM registers, lost at power-off; nothing touches the
 * PY32's own firmware or storage.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PY32_REG_GPIO_M_L    0x03    /* direction, pins 0-7: 1 = output */
#define PY32_REG_GPIO_O_L    0x05    /* output latch, pins 0-7 */
#define PY32_REG_GPIO_PU_L   0x09
#define PY32_REG_GPIO_PD_L   0x0B
#define PY32_REG_GPIO_DRV_L  0x13    /* 1 = open drain */
#define PY32_REG_GPIO_M_H    0x04    /* direction, pins 8-13: 1 = output */
#define PY32_REG_GPIO_PU_H   0x0A    /* pull-up */
#define PY32_REG_GPIO_PD_H   0x0C    /* pull-down */
#define PY32_REG_GPIO_DRV_H  0x14    /* 1 = open drain */
#define PY32_REG_LED_CFG     0x24    /* bits 0-5 LED count, bit 6 refresh */
#define PY32_REG_LED_RAM     0x30    /* RGB565, little endian, 2 bytes per LED */
#define PY32_VM_PIN_BIT      (1u << 0)        /* pin 0: the base's 5 V (servos, LEDs, IR) */
#define PY32_LED_PIN_BIT     (1u << (13 - 8))
#define PY32_LED_COUNT       12
#define PY32_LED_REFRESH     (1u << 6)

static inline bool stackchan_py32_write_allowed(uint8_t reg, size_t n)
{
    if (n == 1 && (reg == PY32_REG_GPIO_M_H || reg == PY32_REG_GPIO_PU_H || reg == PY32_REG_GPIO_PD_H ||
                   reg == PY32_REG_GPIO_DRV_H || reg == PY32_REG_LED_CFG || reg == PY32_REG_GPIO_M_L ||
                   reg == PY32_REG_GPIO_O_L || reg == PY32_REG_GPIO_PU_L || reg == PY32_REG_GPIO_PD_L ||
                   reg == PY32_REG_GPIO_DRV_L)) {
        return true;
    }
    return n > 0 && reg >= PY32_REG_LED_RAM && reg + n <= PY32_REG_LED_RAM + 2 * PY32_LED_COUNT;
}
