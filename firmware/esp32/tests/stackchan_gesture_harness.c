/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Reads "name repeat speed yaw_now pitch_now" lines; prints the plan as
 * "ok total n y,p,ms ..." or "refused <why>". Speeds like "nan" parse via strtof. */
#include <stdio.h>
#include <stdlib.h>

#include "boards/stackchan_gesture.h"

int main(void)
{
    char name[32], sp[32], yn[32], pn[32];
    int repeat;
    while (scanf("%31s %d %31s %31s %31s", name, &repeat, sp, yn, pn) == 5) {
        gesture_plan_t p;
        int g = gesture_from_name(name);
        const char *why = gesture_plan((gesture_id_t)(g < 0 ? GESTURE_COUNT : g), repeat, strtof(sp, NULL),
                                       strtof(yn, NULL), strtof(pn, NULL), &p);
        if (why) {
            printf("refused %s\n", why);
            continue;
        }
        printf("ok %u %d", (unsigned)p.total_ms, p.n);
        for (int i = 0; i < p.n; i++) {
            printf(" %.2f,%.2f,%u", p.frame[i].yaw, p.frame[i].pitch, p.frame[i].ms);
        }
        printf("\n");
    }
    return 0;
}
