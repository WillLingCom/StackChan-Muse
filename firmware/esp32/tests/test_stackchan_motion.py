# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 motion play (components/muse/boards/stackchan_motion.c) on synthetic IMU data: shaking makes it dizzy and
wears off, tilting leans it both ways, a lift and upside-down are recognised, nothing happens while the servos
move or under gentle handling, and a new resting slope becomes the new "up"."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHAKE, LIFT, FLIP, UNFLIP, SLIP = 1, 2, 4, 8, 16


class MotionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "motion"
            subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                            str(ROOT / "tests" / "stackchan_motion_harness.c"),
                            str(ROOT / "components" / "muse" / "boards" / "stackchan_motion.c"), "-lm",
                            "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
        cls.r = {l.split()[0]: (int(l.split()[1]), float(l.split()[2]), float(l.split()[3])) for l in out.splitlines()}

    def test_rest_is_quiet(self) -> None:
        self.assertEqual(self.r["rest"], (0, 0.0, 0.0))

    def test_tilt_both_ways(self) -> None:
        ev, _, tilt = self.r["tilt30"]
        self.assertEqual(ev, 0)
        self.assertAlmostEqual(abs(tilt), 30 / 45, delta=0.05)
        self.assertAlmostEqual(self.r["tilt-30"][2], -tilt, delta=0.02)

    def test_shake_makes_it_dizzy_and_it_wears_off(self) -> None:
        ev, dizzy, _ = self.r["shake"]
        self.assertTrue(ev & SHAKE)
        self.assertFalse(ev & (LIFT | FLIP))
        self.assertGreater(dizzy, 0.7)
        self.assertLess(self.r["after1.5"][1], dizzy)
        self.assertEqual(self.r["after4.5"][1], 0.0)      # worn off
        self.assertTrue(self.r["spin"][0] & SHAKE)          # twisting it back and forth counts too
        self.assertTrue(self.r["side1g"][0] & SHAKE)        # side to side at 1 g (its size barely changes)
        self.assertTrue(self.r["twist250"][0] & SHAKE)      # a moderate twist
        self.assertTrue(self.r["realshake"][0] & SHAKE)     # as measured on the device: 2-3.8 g, 200-320 deg/s
        self.assertGreater(self.r["realshake"][1], 0.9)
        self.assertFalse(self.r["hardlift"][0] & SHAKE)     # a sharp lift (0.3 s) is not a shake

    def test_lift(self) -> None:
        self.assertEqual(self.r["lift"][0], LIFT)

    def test_flip_and_back(self) -> None:
        self.assertEqual(self.r["flip"][0], FLIP)
        self.assertEqual(self.r["unflip"][0], UNFLIP)

    def test_slip(self) -> None:
        self.assertEqual(self.r["slip"][0], SLIP)

    def test_nothing_while_servos_move(self) -> None:
        self.assertEqual(self.r["hold"][:2], (0, 0.0))

    def test_gentle_handling_is_not_a_shake(self) -> None:
        self.assertEqual(self.r["gentle"][0] & SHAKE, 0)

    def test_a_slope_becomes_up(self) -> None:
        self.assertLess(abs(self.r["slope"][2]), 0.1)


if __name__ == "__main__":
    unittest.main()
