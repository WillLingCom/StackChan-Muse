/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * Checks a robot.gesture call's params before anything moves, and remembers
 * recent request ids so a repeated one is refused. Depends only on cJSON, so
 * the host tests run it.
 */
#pragma once

#include <stdbool.h>

#include "cJSON.h"

typedef struct {
    int gesture;     /* gesture_id_t */
    int repeat;      /* 1..3 */
    float speed;     /* 0.5..1.0 */
} stackchan_gesture_args_t;

/* True if params are a valid robot.gesture call; otherwise sets *code
 * ("invalid_params") and *message. Unknown keys are refused, numbers must be
 * finite, repeat a whole number. */
bool stackchan_parse_gesture_args(const cJSON *params, stackchan_gesture_args_t *out, const char **code,
                                  const char **message);

#define STACKCHAN_SEEN_IDS 16
typedef struct {
    char id[STACKCHAN_SEEN_IDS][48];
    int next;
} stackchan_seen_t;

/* True if id was seen among the last STACKCHAN_SEEN_IDS; remembers it otherwise. */
bool stackchan_request_seen(stackchan_seen_t *s, const char *id);
