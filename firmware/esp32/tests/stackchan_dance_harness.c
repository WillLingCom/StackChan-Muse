/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* For each built-in dance prints one line:
 * "name total_ms check frames peak max_light max_pulses_per_s max_tint checksum start_pose end_pose"
 * then "unknown <dance_from_name("salsa")>". */
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "boards/stackchan_dance.h"

static int16_t buf[DANCE_MAX_MS * DANCE_RATE / 1000];

static int channel_max(uint32_t c)
{
    int m = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int v = (c >> sh) & 0xFF;
        m = v > m ? v : m;
    }
    return m;
}

int main(void)
{
    for (int s = 0; s < DANCE_COUNT; s++) {
        uint32_t total = dance_total_ms(s);
        const char *why = dance_check(s);
        dance_synth_t sy;
        dance_synth_init(&sy, s);
        size_t n = 0, k;
        while ((k = dance_synth_render(&sy, buf + n, 320)) > 0) {
            n += k;
        }
        int peak = 0;
        uint32_t sum = 2166136261u;
        for (size_t i = 0; i < n; i++) {
            peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
            sum = (sum ^ (uint16_t)buf[i]) * 16777619u;
        }
        /* Light: brightest channel, and the most pulses (rise and fall by 15+) of
         * either side within any one second. */
        int max_light = 0, max_tint = 0, worst = 0;
        for (int side = 0; side < 2; side++) {
            int peaks_at[4096], np = 0, hi = 0, lo = 0;
            bool rising = true;
            for (int32_t t = -500; t <= (int32_t)total + 500; t += 10) {
                uint32_t l, r;
                dance_light_at(s, t, &l, &r);
                int b = channel_max(side ? r : l);
                max_light = b > max_light ? b : max_light;
                int tint = channel_max(dance_tint_at(s, t));
                max_tint = tint > max_tint ? tint : max_tint;
                if (rising) {
                    hi = b > hi ? b : hi;
                    if (b < hi - 15) {
                        if (np < 4096) {
                            peaks_at[np++] = t;
                        }
                        rising = false;
                        lo = b;
                    }
                } else {
                    lo = b < lo ? b : lo;
                    if (b > lo + 15) {
                        rising = true;
                        hi = b;
                    }
                }
            }
            for (int i = 0; i < np; i++) {
                int c = 0;
                for (int j = i; j < np && peaks_at[j] < peaks_at[i] + 1000; j++) {
                    c++;
                }
                worst = c > worst ? c : worst;
            }
        }
        float y0, p0, y1, p1;
        dance_pose_at(s, 0, &y0, &p0);
        dance_pose_at(s, (int32_t)total, &y1, &p1);
        printf("%s %u %s %zu %d %d %d %d %08x %.1f,%.1f %.1f,%.1f\n", dance_name(s), (unsigned)total,
               why ? "bad" : "ok", n, peak, max_light, worst, max_tint, (unsigned)sum, y0, p0, y1, p1);
    }
    printf("unknown %d\n", dance_from_name("salsa"));
    return 0;
}
