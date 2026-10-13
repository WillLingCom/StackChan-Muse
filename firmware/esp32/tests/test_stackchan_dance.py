# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 dances (components/muse/boards/stackchan_dance.c): every built-in dance passes its own safety check
(limits, speeds, start/end pose), fits the time cap, renders exactly its length without clipping, and its lights
stay soft (a channel cap, at most 3 pulses a second)."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        exe = Path(cls.tmp.name) / "dance"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        str(ROOT / "tests" / "stackchan_dance_harness.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_dance.c"), "-lm", "-o", str(exe)],
                       check=True)
        cls.lines = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
        cls.rows = {l.split()[0]: l.split()[1:] for l in cls.lines[:-1]}

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_both_dances_exist(self) -> None:
        self.assertEqual(set(self.rows), {"happy", "chill"})
        self.assertEqual(self.lines[-1], "unknown -1")

    def test_safety_check_passes(self) -> None:
        for name, r in self.rows.items():
            self.assertEqual(r[1], "ok", name)
            self.assertLessEqual(int(r[0]), 40000)
            self.assertEqual(r[8], "0.0,4.0", name)    # starts at the lift pose
            self.assertEqual(r[9], "0.0,4.0", name)    # and ends there

    def test_audio_is_whole_and_unclipped(self) -> None:
        for name, r in self.rows.items():
            self.assertEqual(int(r[2]), int(r[0]) * 16, name)   # 16 kHz, exactly the dance's length
            self.assertGreater(int(r[3]), 5000, name)           # not silent
            self.assertLess(int(r[3]), 32000, name)             # soft-clipped, never at full scale

    def test_lights_are_soft(self) -> None:
        for name, r in self.rows.items():
            self.assertLessEqual(int(r[4]), 110, name)
            self.assertLessEqual(int(r[5]), 3, name)     # pulses per second: no strobe
            self.assertLessEqual(int(r[6]), 60, name)    # screen tint stays dim

    def test_synth_is_deterministic(self) -> None:
        exe = Path(self.tmp.name) / "dance"
        again = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
        self.assertEqual(again, self.lines)


if __name__ == "__main__":
    unittest.main()
