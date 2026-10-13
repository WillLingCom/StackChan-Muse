/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 */

/*
 * StackChan K151 tools for Muse (registered in link.register's commands_v2,
 * handled from link.invoke): robot.gesture, robot.stop, robot.status. Only
 * the verified preset gestures are exposed; no free-form poses, walking or
 * dancing. Natural language → which tool is Muse's choice; checking,
 * limits, timing and stopping stay on the device.
 */
#pragma once

#include "cJSON.h"
#include "muse_board.h"

void stackchan_tools_register(cJSON *commands);
/* Takes one fresh photo now (on the caller's task) as camera.capture's result. */
cJSON *stackchan_photo_result(void);
cJSON *stackchan_tools_invoke(const char *command, cJSON *params, const char *request_id, muse_tool_done_t done,
                              void *ctx);
