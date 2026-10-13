/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */
#pragma once

/*
 * F9 infrared on the K151: IR LED on GPIO5 (RMT TX, carrier), receiver
 * (demodulating, active low) on GPIO10 (RMT RX), per StackChan-BSP's IR
 * examples. Set up on first use. Learned codes live in NVS (namespace
 * "k151_ir", at most IR_MAX_CODES, with room left for Muse's own data).
 * One operation at a time; all calls block (learn up to its timeout).
 *
 * Infrared is one-way: a send can't tell whether anything obeyed.
 */

#include <stdbool.h>
#include <stddef.h>

#include "stackchan_ir_codec.h"

/* Waits up to timeout_ms for one remote button; NULL with *out filled, or why not. */
const char *stackchan_ir_learn(int timeout_ms, ir_code_t *out);
/* Sends one code once. NULL or why not. */
const char *stackchan_ir_send(const ir_code_t *c);

const char *stackchan_ir_save(const char *name, const ir_code_t *c);
const char *stackchan_ir_load(const char *name, ir_code_t *out);
const char *stackchan_ir_delete(const char *name);
/* Names (and protocols) of the saved codes; returns how many. */
int stackchan_ir_list(char names[][IR_NAME_MAX + 1], uint8_t *protos, int max);

/*
 * Sends a test pattern that is no remote protocol (no appliance acts on it)
 * `repeats` times, 100 ms apart, listening on the robot's own receiver.
 * Writes a JSON summary; NULL or why it couldn't run.
 */
const char *stackchan_ir_selftest(int repeats, char *json, size_t cap);

/* Sends a saved code once while listening on the robot's own receiver, and
 * compares what comes back with the code's first frame (JSON). For checking
 * the transmitted waveform. */
const char *stackchan_ir_echo(const char *name, char *json, size_t cap);

/* Sends a 60 ms carrier burst while reading GPIO5's own pad level back
 * (input enabled alongside the output): highs/lows/samples as JSON. For telling
 * a dead pad from a dead LED. */
const char *stackchan_ir_padcheck(char *json, size_t cap);

bool stackchan_ir_busy(void);
