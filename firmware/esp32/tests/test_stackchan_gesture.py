# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 gesture planner (components/muse/boards/stackchan_gesture.c): every plan stays inside the
soft limits and speed limits, ends at home, lasts at most 5 s, and bad requests are refused."""

from __future__ import annotations

import itertools
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GESTURES = ["nod", "shake", "sway", "look_left", "look_right", "home", "turn_left", "turn_right"]
YAW_LIMIT, PITCH_LIMIT, YAW_SPEED, PITCH_SPEED = 25.0, 30.0, 60.0, 45.0


class GesturePlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "gesture"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        str(ROOT / "tests" / "stackchan_gesture_harness.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_gesture.c"), "-lm",
                        "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def plan(self, *lines: str) -> list[str]:
        out = subprocess.run([str(self.exe)], input="\n".join(lines) + "\n", capture_output=True, text=True,
                             check=True).stdout.splitlines()
        self.assertEqual(len(out), len(lines))
        return out

    def check_plan(self, line: str, yaw0: float, pitch0: float) -> None:
        parts = line.split()
        self.assertEqual(parts[0], "ok", line)
        total, frames = int(parts[1]), [tuple(map(float, f.split(","))) for f in parts[3:]]
        self.assertLessEqual(total, 5000)
        self.assertEqual(sum(f[2] for f in frames), total)
        turn = line_end = frames[-1][:2]
        self.assertTrue(line_end == (0.0, 0.0) or line_end in ((-20.0, 0.0), (20.0, 0.0)), "must end at home (or turned)")
        y, p = yaw0, pitch0
        for fy, fp, ms in frames:
            self.assertLessEqual(abs(fy), YAW_LIMIT)
            self.assertTrue(0.0 <= fp <= PITCH_LIMIT)
            self.assertLessEqual(abs(fy - y) / (ms / 1000), YAW_SPEED + 0.5)
            self.assertLessEqual(abs(fp - p) / (ms / 1000), PITCH_SPEED + 0.5)
            y, p = fy, fp

    def test_every_accepted_plan_respects_the_limits(self) -> None:
        cases = list(itertools.product(GESTURES, [1, 2, 3], ["0.5", "0.75", "1"], [(0, 0), (8, -2), (-20, 3)]))
        lines = [f"{g} {r} {s} {y} {p}" for g, r, s, (y, p) in cases]
        accepted = 0
        for (g, r, s, (y, p)), out in zip(cases, self.plan(*lines)):
            if out.startswith("ok"):
                accepted += 1
                self.check_plan(out, y, p)
            else:
                self.assertEqual(out, "refused longer than 5 s", (g, r, s))
        self.assertGreater(accepted, 30)

    def test_defaults_are_accepted(self) -> None:
        for out in self.plan(*(f"{g} 1 1 0 0" for g in GESTURES)):
            self.assertTrue(out.startswith("ok"), out)

    def test_far_start_is_slowed_not_rushed(self) -> None:
        out = self.plan("home 1 1 24 25")[0]
        self.check_plan(out, 24, 25)

    def test_bad_requests_are_refused_with_a_reason(self) -> None:
        out = self.plan("dance 1 1 0 0", "nod 0 1 0 0", "nod 4 1 0 0", "nod 1 nan 0 0", "nod 1 inf 0 0",
                        "nod 1 1.5 0 0", "nod 1 0.2 0 0", "nod 1 1 nan 0", "nod 1 1 0 inf", "nod 1 1 200 0")
        self.assertEqual(out, ["refused unknown gesture", "refused repeat must be 1-3", "refused repeat must be 1-3",
                               "refused speed must be 0.5-1.0", "refused speed must be 0.5-1.0",
                               "refused speed must be 0.5-1.0", "refused speed must be 0.5-1.0",
                               "refused current pose unknown or implausible",
                               "refused current pose unknown or implausible",
                               "refused current pose unknown or implausible"])

    def test_too_long_is_refused(self) -> None:
        self.assertEqual(self.plan("sway 3 0.5 0 0"), ["refused longer than 5 s"])

    def test_local_turns_end_facing_the_talker(self) -> None:
        left, right = self.plan("turn_left 1 1 0 0", "turn_right 1 1 0 0")
        self.check_plan(left, 0, 0)
        self.assertTrue(left.endswith(",0.00," + left.split(",")[-1]) and " -20.00,0.00," in left)
        self.assertIn(" 20.00,0.00,", right)

    def test_look_directions_are_the_robots(self) -> None:
        left, right = self.plan("look_left 1 1 0 0", "look_right 1 1 0 0")
        self.assertLess(float(left.split()[3].split(",")[0]), 0)    # robot's left = yaw - (viewer's right)
        self.assertGreater(float(right.split()[3].split(",")[0]), 0)


if __name__ == "__main__":
    unittest.main()
