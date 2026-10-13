/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * The base's 12 RGB LEDs, driven by the PY32 (left 0-5, right 6-11), set up
 * on first use as the factory firmware does. Colours are RGB888, each channel
 * capped at STACKCHAN_RGB_MAX. Not thread-safe: one user at a time (the dance).
 */

#include <stdbool.h>
#include <stdint.h>

#define STACKCHAN_RGB_MAX 128

bool stackchan_rgb_set(uint32_t left, uint32_t right);
void stackchan_rgb_off(void);
