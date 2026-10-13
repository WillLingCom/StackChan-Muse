/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#include "stackchan_ir_codec.h"

#include <string.h>

#define NEC_LEAD_MARK   9000
#define NEC_LEAD_SPACE  4500
#define NEC_BIT_MARK    560
#define NEC_ZERO_SPACE  560
#define NEC_ONE_SPACE   1690
#define NEC_EDGES       67      /* leader 2 + 32 bits x 2 + stop mark */

static bool near(uint16_t v, uint16_t want)
{
    return v >= want - want / 4 && v <= want + want / 4;
}

bool ir_decode_nec(const uint16_t *us, size_t n, uint32_t *code)
{
    if (n < NEC_EDGES || !near(us[0], NEC_LEAD_MARK) || !near(us[1], NEC_LEAD_SPACE)) {
        return false;
    }
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) {
        uint16_t mark = us[2 + 2 * i], space = us[3 + 2 * i];
        if (!near(mark, NEC_BIT_MARK)) {
            return false;
        }
        if (near(space, NEC_ONE_SPACE)) {
            v |= 1u << i;
        } else if (!near(space, NEC_ZERO_SPACE)) {
            return false;
        }
    }
    if (!near(us[66], NEC_BIT_MARK)) {
        return false;
    }
    *code = v;
    return true;
}

size_t ir_encode_nec(uint32_t code, uint16_t *us, size_t cap)
{
    if (cap < NEC_EDGES) {
        return 0;
    }
    us[0] = NEC_LEAD_MARK;
    us[1] = NEC_LEAD_SPACE;
    for (int i = 0; i < 32; i++) {
        us[2 + 2 * i] = NEC_BIT_MARK;
        us[3 + 2 * i] = (code >> i) & 1 ? NEC_ONE_SPACE : NEC_ZERO_SPACE;
    }
    us[66] = NEC_BIT_MARK;
    return NEC_EDGES;
}

const char *ir_code_check(const ir_code_t *c)
{
    if (c->proto != IR_PROTO_RAW && c->proto != IR_PROTO_NEC) {
        return "unknown protocol";
    }
    if (c->repeat < 1 || c->repeat > IR_MAX_REPEAT ||
        (c->repeat > 1 && (c->gap_us < IR_FRAME_GAP_US || c->gap_us > IR_MAX_US))) {
        return "bad repeat";
    }
    if (c->carrier_hz < 30000 || c->carrier_hz > 60000) {
        return "carrier out of range";
    }
    if (c->n < IR_MIN_EDGES || c->n > IR_MAX_EDGES || !(c->n & 1)) {
        return "bad edge count";
    }
    uint32_t total = 0;
    for (size_t i = 0; i < c->n; i++) {
        if (c->us[i] < IR_MIN_US || c->us[i] > IR_MAX_US) {
            return "duration out of range";
        }
        total += c->us[i];
    }
    if (total > IR_MAX_FRAME_US) {
        return "frame too long";
    }
    if (c->proto == IR_PROTO_NEC) {
        uint32_t v;
        if (!ir_decode_nec(c->us, c->n, &v) || v != c->nec) {
            return "NEC timing does not match its code";
        }
    }
    return NULL;
}

size_t ir_first_frame(const uint16_t *us, size_t n)
{
    for (size_t i = 1; i < n; i += 2) {   /* spaces sit at odd positions */
        if (us[i] >= IR_FRAME_GAP_US) {
            return i;                    /* the marks and spaces before it, ending on a mark */
        }
    }
    return n;
}

const char *ir_code_from_capture(const uint16_t *us, size_t n, ir_code_t *out)
{
    memset(out, 0, sizeof(*out));
    out->carrier_hz = IR_CARRIER_HZ;
    /* How many frames the press sent (to send as many, up to IR_MAX_REPEAT) and the first gap. */
    size_t frames = 1;
    for (size_t i = 1; i < n; i += 2) {
        if (us[i] >= IR_FRAME_GAP_US) {
            if (frames == 1) {
                out->gap_us = us[i] > IR_MAX_US ? IR_MAX_US : us[i];
            }
            frames++;
        }
    }
    out->repeat = (uint8_t)(frames > IR_MAX_REPEAT ? IR_MAX_REPEAT : frames);
    if (out->repeat == 1) {
        out->gap_us = 0;
    }
    n = ir_first_frame(us, n);
    if (n > 0 && !(n & 1)) {
        n--;   /* a trailing space is only the gap before silence */
    }
    if (n < IR_MIN_EDGES) {
        return "too short: not a remote's signal";
    }
    if (n > IR_MAX_EDGES) {
        return "too long to keep";
    }
    for (size_t i = 0; i < n; i++) {   /* the receiver's own readings, before any correction */
        if (us[i] < IR_MIN_US || us[i] > IR_MAX_US) {
            return "duration out of range";
        }
    }
    uint32_t nec;
    if (ir_decode_nec(us, n, &nec)) {
        out->repeat = 1;   /* NEC repeats are short "held" codes, not the frame again */
        out->gap_us = 0;
        out->proto = IR_PROTO_NEC;
        out->nec = nec;
        out->n = (uint16_t)ir_encode_nec(nec, out->us, IR_MAX_EDGES);   /* clean timing */
    } else {
        out->proto = IR_PROTO_RAW;
        out->n = (uint16_t)n;
        for (size_t i = 0; i < n; i++) {   /* back to what the remote sent */
            int v = (int)us[i] + ((i & 1) ? IR_MARK_EXCESS_US : -IR_MARK_EXCESS_US);
            out->us[i] = (uint16_t)(v < IR_MIN_US ? IR_MIN_US : v > IR_MAX_US ? IR_MAX_US : v);
        }
    }
    return ir_code_check(out);
}

bool ir_name_ok(const char *name)
{
    size_t len = name ? strlen(name) : 0;
    if (len == 0 || len > IR_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)name[i] < 0x20 || name[i] == 0x7F) {
            return false;
        }
    }
    return true;
}

size_t ir_code_pack(const ir_code_t *c, uint8_t *buf, size_t cap)
{
    size_t edges = c->proto == IR_PROTO_RAW ? c->n : 0;
    size_t len = 13 + 2 * edges;
    if (cap < len || ir_code_check(c)) {
        return 0;
    }
    buf[0] = 2;   /* version */
    buf[1] = c->proto;
    buf[2] = (uint8_t)c->carrier_hz;
    buf[3] = (uint8_t)(c->carrier_hz >> 8);
    for (int i = 0; i < 4; i++) {
        buf[4 + i] = (uint8_t)(c->nec >> (8 * i));
    }
    buf[8] = (uint8_t)c->n;
    buf[9] = (uint8_t)(c->n >> 8);
    buf[10] = c->repeat;
    buf[11] = (uint8_t)c->gap_us;
    buf[12] = (uint8_t)(c->gap_us >> 8);
    for (size_t i = 0; i < edges; i++) {
        buf[13 + 2 * i] = (uint8_t)c->us[i];
        buf[14 + 2 * i] = (uint8_t)(c->us[i] >> 8);
    }
    return len;
}

bool ir_code_unpack(const uint8_t *buf, size_t len, ir_code_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < 10 || (buf[0] != 1 && buf[0] != 2)) {
        return false;
    }
    size_t hdr = buf[0] == 1 ? 10 : 13;   /* version 1: one frame, no repeat fields */
    if (len < hdr) {
        return false;
    }
    out->repeat = hdr == 13 ? buf[10] : 1;
    out->gap_us = hdr == 13 ? (uint16_t)(buf[11] | buf[12] << 8) : 0;
    out->proto = buf[1];
    out->carrier_hz = (uint16_t)(buf[2] | buf[3] << 8);
    out->nec = (uint32_t)buf[4] | (uint32_t)buf[5] << 8 | (uint32_t)buf[6] << 16 | (uint32_t)buf[7] << 24;
    uint16_t n = (uint16_t)(buf[8] | buf[9] << 8);
    if (out->proto == IR_PROTO_NEC) {
        if (len != hdr) {
            return false;
        }
        out->n = (uint16_t)ir_encode_nec(out->nec, out->us, IR_MAX_EDGES);
    } else {
        if (n > IR_MAX_EDGES || len != hdr + 2u * n) {
            return false;
        }
        out->n = n;
        for (size_t i = 0; i < n; i++) {
            out->us[i] = (uint16_t)(buf[hdr + 2 * i] | buf[hdr + 1 + 2 * i] << 8);
        }
    }
    return ir_code_check(out) == NULL;
}
