# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""The K151 base-controller write whitelist (components/muse/boards/stackchan_py32_regs.h): only the factory's
writes - the low-byte config of pin 0 (the base's power enable), the high-byte config of the RGB LED pin, the LED
config and the 12 LEDs' RAM; never IRQ, ADC, PWM or the high-byte output latch."""

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
#include "boards/stackchan_py32_regs.h"
int main(void) {
    for (unsigned a = 0; a < 256; a++)
        for (unsigned n = 0; n <= 64; n++)
            if (stackchan_py32_write_allowed((uint8_t)a, n)) printf("%u %u\n", a, n);
    return 0;
}
"""


class Py32WhitelistTest(unittest.TestCase):
    def test_only_the_led_registers(self) -> None:
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
        self.assertEqual(touched, {0x03, 0x05, 0x09, 0x0B, 0x13, 0x04, 0x0A, 0x0C, 0x14, 0x24} | set(range(0x30, 0x48)))
        self.assertIn((0x30, 24), allowed)       # all 12 LEDs at once
        self.assertNotIn((0x30, 25), allowed)
        self.assertNotIn((0x04, 2), allowed)     # single config bytes only
        self.assertNotIn((0x03, 2), allowed)
        for never in (0x00, 0x02, 0x06, 0x0D, 0x0E, 0x0F, 0x11, 0x12, 0x15, 0x1B, 0x25):
            self.assertNotIn(never, touched)     # UID/version, high outputs, IRQ, ADC, PWM


if __name__ == "__main__":
    unittest.main()
