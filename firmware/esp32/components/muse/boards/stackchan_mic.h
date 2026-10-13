/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * Short raw stereo recordings from both mic slots (muse_audio_set_tap), for
 * checking the mics and calibrating direction finding offline. Only on an
 * explicit console request; nothing is kept on the device afterwards.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define STACKCHAN_MIC_MAX_S 5

/* Records `seconds` (1..STACKCHAN_MIC_MAX_S) of interleaved stereo at 16 kHz
 * into a new PSRAM buffer (*frames stereo frames); free() it. NULL on failure
 * or if no audio arrived (the voice task reads the mics while idle). */
int16_t *stackchan_mic_record(int seconds, size_t *frames);
