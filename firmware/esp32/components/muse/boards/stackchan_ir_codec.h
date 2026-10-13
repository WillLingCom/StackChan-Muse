/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * F9 infrared, the pure part (host-tested): a learned code is the receiver's
 * demodulated timing, mark, space, mark, ... in microseconds, ending with a
 * mark. NEC frames are recognised (and re-generated cleanly to send); anything
 * else is kept raw and sent back as captured on the assumed carrier.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_EDGES     383     /* the receiver's 192 RMT symbols, less the final idle */
#define IR_MIN_EDGES     3
#define IR_MIN_US        80
#define IR_MAX_US        30000
#define IR_MAX_FRAME_US  400000
#define IR_CARRIER_HZ    38000   /* demodulation loses the carrier: assumed */
#define IR_FRAME_GAP_US  8000    /* a space this long ends a frame: what follows is a repeat (one press
                                  * often sends several); NEC's longest space inside a frame is 4.5 ms */
#define IR_MAX_REPEAT    3       /* frames sent per press (as the remote repeats them) */
#define IR_MARK_EXCESS_US 90     /* the receiver stretches marks (and shortens spaces) by about this
                                  * (measured: +91 us on its own LED's echo); taken off raw captures */
#define IR_NAME_MAX      31      /* bytes of UTF-8 */
#define IR_MAX_CODES     10

typedef enum {
    IR_PROTO_RAW = 0,
    IR_PROTO_NEC = 1,
} ir_proto_t;

typedef struct {
    uint8_t proto;
    uint16_t carrier_hz;
    uint32_t nec;            /* NEC: the 32 bits as sent, first bit in bit 0 */
    uint8_t repeat;          /* frames per send, 1..IR_MAX_REPEAT */
    uint16_t gap_us;         /* space between repeated frames (repeat > 1) */
    uint16_t n;              /* edges in us[], one frame */
    uint16_t us[IR_MAX_EDGES];
} ir_code_t;

/* A NEC frame (9 ms + 4.5 ms leader, 32 bits, stop mark), +-25 %. */
bool ir_decode_nec(const uint16_t *us, size_t n, uint32_t *code);
/* The timing for a NEC frame; returns the edge count (67) or 0 if cap is too small. */
size_t ir_encode_nec(uint32_t code, uint16_t *us, size_t cap);

/* Edges in the first frame of a capture (up to the first space >= IR_FRAME_GAP_US). */
size_t ir_first_frame(const uint16_t *us, size_t n);
/* Turns a capture into a code: its first frame, NEC if it is one, raw otherwise. NULL or why not. */
const char *ir_code_from_capture(const uint16_t *us, size_t n, ir_code_t *out);
/* Sendable: edge count and every duration in range, plausible total. NULL or why not. */
const char *ir_code_check(const ir_code_t *c);

/* A name: 1..IR_NAME_MAX bytes, no control characters. */
bool ir_name_ok(const char *name);

/* Storage form: version, proto, carrier, nec, n, repeat, gap, then the edges (raw only). */
size_t ir_code_pack(const ir_code_t *c, uint8_t *buf, size_t cap);
bool ir_code_unpack(const uint8_t *buf, size_t len, ir_code_t *out);
#define IR_PACK_MAX (13 + 2 * IR_MAX_EDGES)
