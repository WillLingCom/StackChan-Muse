# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""The K151 servo write whitelist (components/muse/boards/stackchan_servo_regs.h):
only torque and goal registers, never EEPROM (ID, baud, angle limits) or the lock."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r"""
#include <stdio.h>
#include "boards/stackchan_servo_regs.h"
int main(void) {
    for (unsigned a = 0; a < 80; a++)
        for (unsigned n = 0; n <= 8; n++)
            if (stackchan_servo_write_allowed(a, n)) printf("%u %u\n", a, n);
    return 0;
}
"""


class ServoWhitelistTest(unittest.TestCase):
    def test_only_torque_and_goal_registers(self) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "w.c", Path(tmp) / "w"
            src.write_text(PROGRAM)
            subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                            str(src), "-o", str(exe)], check=True)
            allowed = {tuple(map(int, l.split())) for l in subprocess.run([str(exe)], capture_output=True, text=True,
                                                                           check=True).stdout.split("\n") if l}
        touched = {a + i for a, n in allowed for i in range(n)}
        self.assertEqual(touched, set(range(40, 48)))
        self.assertIn((40, 1), allowed)          # torque
        self.assertIn((42, 6), allowed)          # goal position, time, speed
        for eeprom in (5, 6, 9, 10, 11, 12):     # ID, baud, angle limits
            self.assertFalse(any(a <= eeprom < a + n for a, n in allowed))
        self.assertFalse(any(a <= 48 < a + n for a, n in allowed))   # EEPROM lock
        self.assertNotIn((40, 0), allowed)


if __name__ == "__main__":
    unittest.main()
