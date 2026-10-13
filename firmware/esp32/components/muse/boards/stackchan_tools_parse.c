/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

#include "stackchan_tools_parse.h"

#include <math.h>
#include <string.h>

#include "stackchan_gesture.h"

static bool fail(const char **code, const char **message, const char *why)
{
    *code = "invalid_params";
    *message = why;
    return false;
}

bool stackchan_parse_gesture_args(const cJSON *params, stackchan_gesture_args_t *out, const char **code,
                                  const char **message)
{
    *out = (stackchan_gesture_args_t){ .gesture = -1, .repeat = 1, .speed = 1.0f };
    if (!cJSON_IsObject(params)) {
        return fail(code, message, "gesture is required");
    }
    for (const cJSON *p = params->child; p; p = p->next) {
        if (!p->string || (strcmp(p->string, "gesture") && strcmp(p->string, "repeat") && strcmp(p->string, "speed"))) {
            return fail(code, message, "unknown parameter (allowed: gesture, repeat, speed)");
        }
    }
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(params, "gesture");
    if (!cJSON_IsString(g) || !g->valuestring) {
        return fail(code, message, "gesture must be a string");
    }
    out->gesture = gesture_from_name(g->valuestring);
    if (out->gesture < 0 || out->gesture >= GESTURE_PUBLIC_COUNT) {
        return fail(code, message, "unknown gesture (nod, shake, sway, look_left, look_right, home)");
    }
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(params, "repeat");
    if (r) {
        if (!cJSON_IsNumber(r) || !isfinite(r->valuedouble) || r->valuedouble != floor(r->valuedouble) ||
            r->valuedouble < 1 || r->valuedouble > GESTURE_MAX_REPEAT) {
            return fail(code, message, "repeat must be a whole number 1-3");
        }
        out->repeat = (int)r->valuedouble;
    }
    const cJSON *sp = cJSON_GetObjectItemCaseSensitive(params, "speed");
    if (sp) {
        if (!cJSON_IsNumber(sp) || !isfinite(sp->valuedouble) || sp->valuedouble < 0.5 || sp->valuedouble > 1.0) {
            return fail(code, message, "speed must be a number 0.5-1.0");
        }
        out->speed = (float)sp->valuedouble;
    }
    return true;
}

bool stackchan_request_seen(stackchan_seen_t *s, const char *id)
{
    if (!id || !id[0]) {
        return false;
    }
    for (int i = 0; i < STACKCHAN_SEEN_IDS; i++) {
        if (!strncmp(s->id[i], id, sizeof(s->id[i]) - 1) && s->id[i][0]) {
            return true;
        }
    }
    strncpy(s->id[s->next], id, sizeof(s->id[s->next]) - 1);
    s->id[s->next][sizeof(s->id[s->next]) - 1] = '\0';
    s->next = (s->next + 1) % STACKCHAN_SEEN_IDS;
    return false;
}
