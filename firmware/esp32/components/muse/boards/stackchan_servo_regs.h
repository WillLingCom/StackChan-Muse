/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Register maps and wire protocol follow M5Stack's StackChan firmware
 * (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD):
 * hal/drivers/PY32IOExpander_Class and hal/drivers/FTServo_Arduino (SCSCL).
 */

/* SCSCL registers this firmware may write: torque (40) and goal position,
 * time and speed (42-47). Pure, so the host tests check it. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCS_REG_TORQUE      40
#define SCS_REG_GOAL        42      /* position, time (ms), speed: 42-47, big-endian */
#define SCS_WRITE_FIRST     40      /* below: EEPROM (ID, baud, angle limits) */
#define SCS_WRITE_LAST      47      /* 48: the EEPROM lock */

static inline bool stackchan_servo_write_allowed(unsigned addr, unsigned len)
{
    return len >= 1 && len <= 6 && addr >= SCS_WRITE_FIRST && addr + len - 1 <= SCS_WRITE_LAST;
}
