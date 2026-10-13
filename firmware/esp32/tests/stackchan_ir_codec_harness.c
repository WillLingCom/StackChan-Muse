/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
/* Reads lines "capture d1 d2 ..." (durations in us), "name <text>" or "nec <hex>";
 * prints for a capture "nec <hex>" / "raw <n>" / "refused <why>", plus " packed <len> roundtrip <ok|bad>";
 * for a name "ok"/"bad"; for nec the encoded edge count and its decode. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boards/stackchan_ir_codec.h"

int main(void)
{
    static char line[16384];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "name ", 5)) {
            printf("%s\n", ir_name_ok(line + 5) ? "ok" : "bad");
            continue;
        }
        if (!strcmp(line, "name")) {
            printf("%s\n", ir_name_ok("") ? "ok" : "bad");
            continue;
        }
        if (!strncmp(line, "nec ", 4)) {
            uint16_t us[IR_MAX_EDGES];
            uint32_t code = (uint32_t)strtoul(line + 4, NULL, 16), back = 0;
            size_t n = ir_encode_nec(code, us, IR_MAX_EDGES);
            bool ok = ir_decode_nec(us, n, &back);
            printf("%zu %s %08x\n", n, ok ? "ok" : "bad", (unsigned)back);
            continue;
        }
        static uint16_t us[1024];
        size_t n = 0;
        char *p = line + strlen("capture");
        while (*p && n < 1024) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end == p) {
                break;
            }
            us[n++] = (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
            p = end;
        }
        static ir_code_t c, back;
        const char *why = ir_code_from_capture(us, n, &c);
        if (why) {
            printf("refused %s\n", why);
            continue;
        }
        if (c.proto == IR_PROTO_NEC) {
            printf("nec %08x", (unsigned)c.nec);
        } else {
            printf("raw %u", c.n);
        }
        if (c.repeat > 1) {
            printf(" x%u gap %u", c.repeat, c.gap_us);
        }
        if (getenv("IR_DUMP")) {
            for (size_t i = 0; i < c.n; i++) {
                printf("%s%u", i ? "," : " us ", c.us[i]);
            }
        }
        static uint8_t buf[IR_PACK_MAX];
        size_t len = ir_code_pack(&c, buf, sizeof(buf));
        bool rt = len && ir_code_unpack(buf, len, &back) && back.proto == c.proto && back.n == c.n &&
                  back.nec == c.nec && back.repeat == c.repeat && back.gap_us == c.gap_us && !memcmp(back.us, c.us, c.n * sizeof(c.us[0]));
        bool trunc = len > 0 && ir_code_unpack(buf, len - 1, &back);
        printf(" packed %zu roundtrip %s truncated %s\n", len, rt ? "ok" : "bad", trunc ? "accepted" : "refused");
    }
    return 0;
}
