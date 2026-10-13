/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * Register maps and wire protocol follow M5Stack's StackChan firmware
 * (m5stack/StackChan @1b57655, MIT, Copyright 2026 M5Stack Technology CO LTD):
 * hal/drivers/PY32IOExpander_Class and hal/drivers/FTServo_Arduino (SCSCL).
 */

/*
 * StackChan K151 body: the PY32 base controller's servo-power pin (read only)
 * and the two SCSCL servos.
 *
 * Reading (S3 phase 1) uses PY32 register reads and SCS READ packets only.
 * Moving (S3 phase 2a) is one supervised nudge on explicit request
 * (stackchan_body_nudge): servo writes go through a whitelist of the torque
 * (40) and goal position/time/speed (42-47) registers; EEPROM (below 40, the
 * angle limits, ID and baud) and the lock (48) are refused. The PY32 is never
 * written.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define STACKCHAN_YAW_ID   1    /* horizontal */
#define STACKCHAN_PITCH_ID 2    /* vertical */

typedef struct {
    bool ok;                 /* answered with a valid checksum */
    uint8_t id;
    uint16_t version;        /* reg 3-4 */
    uint16_t min_limit, max_limit;   /* reg 9-12, raw */
    uint8_t cw_dead, ccw_dead;       /* reg 26-27: the dead band, ticks */
    bool torque;             /* reg 40 */
    uint16_t position;       /* reg 56-57, raw 0..1023 */
    int16_t speed;           /* reg 58-59, sign-magnitude bit 15 */
    int16_t load;            /* reg 60-61, sign-magnitude bit 10 */
    uint8_t voltage_dv;      /* reg 62, 0.1 V */
    uint8_t temperature_c;   /* reg 63 */
    bool moving;             /* reg 66 */
    int16_t current;         /* reg 69-70, sign-magnitude bit 15 */
} stackchan_servo_status_t;

typedef struct {
    bool py32_ok;
    uint8_t py32_version;
    bool vm_output;          /* pin 0 set as an output */
    bool vm_level;           /* its output latch */
    bool vm_input;           /* the level it reads back */
    stackchan_servo_status_t servo[2];   /* yaw, pitch */
} stackchan_body_status_t;

/* Adds the PY32 on the bus and installs UART1 for the servos. Reads only. */
esp_err_t stackchan_body_start(i2c_master_bus_handle_t bus);
/* Reads everything once (about 10 ms). */
void stackchan_body_read(stackchan_body_status_t *out);
/* The status as JSON, with angles from the calibrated zeros. */
int stackchan_body_json(const stackchan_body_status_t *st, char *buf, size_t cap);

/*
 * One supervised nudge of one axis (0 yaw, 1 pitch), on its own task: checks
 * power, temperature and torque-off, sets the goal to where the servo is
 * before enabling torque (no jump to a stale goal), moves STACKCHAN_NUDGE_TICKS
 * (pitch: up only) over a second, back over a second, verifying each against
 * feedback, then turns torque off where it started. Aborts on a stop request,
 * stall-like load or current, or a missed target: holds where it is, then
 * torque off. The result goes to the callback as JSON. False if refused
 * (busy, bad axis).
 */
#define STACKCHAN_NUDGE_TICKS 32    /* 10 degrees (user approved; 10 ticks was below the servos' resolution) */
typedef void (*stackchan_body_result_cb_t)(const char *json, void *ctx);
bool stackchan_body_nudge(int axis, int dir, stackchan_body_result_cb_t cb);
/*
 * Runs a planned gesture (stackchan_gesture.h) on both servos, on its own
 * task, and reports JSON to the callback when done. Refused while another
 * move runs, while a hand is on the head, or if the servos aren't healthy.
 * Ends at home, pitch back on its rest, torque off. On a stop, a touch or a
 * fault it holds the pose (2 s, or until the hand leaves), lowers the pitch
 * slowly onto its rest, then turns torque off: the head never drops.
 */
bool stackchan_body_gesture(int gesture, int repeat, float speed, stackchan_body_result_cb_t cb, void *ctx);
/* Stops a nudge or gesture in progress (see above). Safe from any task. */
void stackchan_body_stop(void);
bool stackchan_body_moving(void);

/*
 * F8: follows a moving target (a dance). pose() gives each 20 ms goal in
 * degrees (yaw as above, pitch above the rest pose) and returns 1, or 0 when
 * finished, or -1 with *why to abort. ready() is called once the head has
 * glided to the first pose. Limits and speed are capped whatever pose() asks;
 * a stop, a hand on the head, a fault or max_ms abort as for gestures (hold,
 * lower slowly); it ends at home with torque off. cb gets a JSON report.
 */
typedef int (*stackchan_follow_pose_t)(void *ctx, float *yaw, float *pitch, const char **why);
bool stackchan_body_follow(stackchan_follow_pose_t pose, void (*ready)(void *ctx), uint32_t max_ms,
                           stackchan_body_result_cb_t cb, void *ctx);

/* The PY32 base controller: reads, and writes only within stackchan_py32_regs.h's whitelist. */
bool stackchan_py32_read(uint8_t reg, uint8_t *val);
bool stackchan_py32_write(uint8_t reg, const uint8_t *data, size_t n);
