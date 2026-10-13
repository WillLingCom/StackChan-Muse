/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * StackChan K151 camera: the CoreS3's GC0308 over DVP (esp_video, through
 * the m5stack_core_s3 BSP), as a camera_driver_t (components/camera). Stills
 * only, on demand: each capture starts the stream, drops frames while the
 * exposure settles (so the photo is new), encodes one 320x240 frame to JPEG
 * in software (esp_new_jpeg; the S3 has no JPEG hardware) and stops.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Registers the camera with camera_register(); it starts on first capture. */
void stackchan_camera_register(void);
/* Mirror the image horizontally from the next photo on (default:
 * CONFIG_MUSE_STACKCHAN_CAMERA_HFLIP); for checking which way is right. */
void stackchan_camera_set_hflip(bool on);
