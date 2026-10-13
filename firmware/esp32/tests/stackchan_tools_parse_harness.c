/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Each stdin line is a params JSON (or "id:<request id>"); prints ok/err per line. */
#include <stdio.h>
#include <string.h>

#include "boards/stackchan_tools_parse.h"

int main(void)
{
    static stackchan_seen_t seen;
    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "id:", 3)) {
            printf("%s\n", stackchan_request_seen(&seen, line + 3) ? "dup" : "new");
            continue;
        }
        cJSON *p = !strcmp(line, "null") ? NULL : cJSON_Parse(line);
        stackchan_gesture_args_t a;
        const char *code = "", *msg = "";
        if (stackchan_parse_gesture_args(p, &a, &code, &msg)) {
            printf("ok %d %d %.2f\n", a.gesture, a.repeat, a.speed);
        } else {
            printf("err %s: %s\n", code, msg);
        }
        cJSON_Delete(p);
    }
    return 0;
}
