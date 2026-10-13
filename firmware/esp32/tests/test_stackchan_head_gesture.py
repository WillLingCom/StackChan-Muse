# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""StackChan K151 head touch gestures (components/muse/boards/stackchan_head_gesture.c):
the Si12T's three 2-bit zones, debounced presses and releases, one swipe per touch."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# OUTPUT1 values: zone 0 in bits 1:0, zone 1 in 3:2, zone 2 in 5:4.
Z0, Z1, Z2 = 0x02, 0x08, 0x20     # one zone at level 2
NONE = 0x00
# Positions as the K151 logs them.
P = {-100: 0x03, -75: 0x07, -50: 0x0F, 0: 0x0C, 25: 0x1C, 50: 0x3C, 75: 0x34, 100: 0x30}


def seq(*steps: tuple[int, int]) -> list[int]:
    """(position, samples) pairs -> OUTPUT1 samples."""
    return [P[p] for p, n in steps for _ in range(n)]


class HeadGestureTest(unittest.TestCase):
    binary: Path

    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / "head_gesture"
        proc = subprocess.run(
            [*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "components" / "muse"),
             str(ROOT / "tests" / "stackchan_head_gesture_harness.c"),
             str(ROOT / "components" / "muse" / "boards" / "stackchan_head_gesture.c"),
             "-o", str(cls.binary)],
            capture_output=True, text=True, check=False)
        if proc.returncode:
            raise AssertionError(proc.stderr)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_samples(self, samples: list[int]) -> list[str]:
        out = subprocess.run([str(self.binary)], input="\n".join(f"{s:x}" for s in samples),
                             capture_output=True, text=True, check=True).stdout
        return [line.split()[0] for line in out.splitlines()]

    def events(self, samples: list[int]) -> list[str]:
        return [e for e in self.run_samples(samples) if e != "-"]

    def test_position_weights(self) -> None:
        out = subprocess.run([str(self.binary)], input=f"{Z0:x}\n{Z1:x}\n{Z2:x}\n{Z0 | Z2:x}\n0\n",
                             capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out[1::2], ["-100", "0", "100", "0", "0"])

    def test_press_and_release_need_two_samples(self) -> None:
        self.assertEqual(self.run_samples([Z1, Z1, Z1, NONE, NONE]), ["-", "press", "-", "-", "release"])

    def test_single_noise_sample_is_ignored(self) -> None:
        self.assertEqual(self.events([NONE, Z1, NONE, NONE, Z2, NONE]), [])

    def test_single_dropout_does_not_release(self) -> None:
        self.assertEqual(self.events([Z1, Z1, NONE, Z1, Z1, NONE, NONE]), ["press", "release"])

    # A press is confirmed on the 2nd sample, then STACKCHAN_HEAD_LAND (2) samples let the
    # hand settle. A swipe is a move of more than 60 within 12 samples, reported once the
    # hand has stayed on for 3 more samples.
    HOLD = 3

    def test_swipe_towards_zone_2(self) -> None:
        self.assertEqual(self.events([Z0] * 4 + [Z1, Z2] + [Z2] * self.HOLD + [NONE, NONE]),
                         ["press", "up", "release"])

    def test_swipe_towards_zone_0(self) -> None:
        self.assertEqual(self.events([Z2] * 4 + [Z1, Z0] + [Z0] * self.HOLD + [NONE, NONE]),
                         ["press", "down", "release"])

    def test_one_swipe_per_touch(self) -> None:
        self.assertEqual(self.events([Z0] * 4 + [Z2] * 4 + [Z0] * 4 + [Z2] * 4 + [NONE, NONE]),
                         ["press", "up", "release"])

    def test_resting_hand_spreading_is_not_a_swipe(self) -> None:
        # From the K151 log (B1): lands on zone 2 (0x30), spreads to 0x3c, then 0x0f.
        self.assertEqual(self.events([0x30, 0x30, 0x3C, 0x0F] + [0x0F] * 10 + [NONE, NONE]),
                         ["press", "release"])

    def test_lift_off_is_not_a_swipe(self) -> None:
        # From the K151 log: a resting hand (2-3 s) shrinks to one side as it lifts,
        # then the release ~150 ms later.
        self.assertEqual(self.events([0x3F] * 50 + [0x30, 0x30, NONE, NONE]), ["press", "release"])
        self.assertEqual(self.events([0x0F] * 50 + [0x3C, 0x30, NONE, NONE]), ["press", "release"])
        # Session 3 B1 rests that ended in short runs: +60 +75 +100 spans 40.
        self.assertEqual(self.events(seq((50, 40), (75, 20), (100, 2)) + [NONE, NONE]), ["press", "release"])

    def test_stroke_that_lifts_off_at_the_far_side(self) -> None:
        # Session 3 touch at 93785 ms, 1.2 s: back to front, released on arrival.
        s1 = seq((25, 2), (-50, 2), (-100, 3), (-75, 2), (-50, 2), (50, 2), (100, 1))
        self.assertEqual(self.events(s1 + [NONE, NONE, NONE]), ["press", "up", "release"])
        # Session 3 touch at 95383 ms, 0.8 s.
        s2 = seq((50, 2), (-50, 3), (25, 3), (50, 2), (100, 2))
        self.assertEqual(self.events(s2 + [NONE, NONE, NONE]), ["press", "up", "release"])
        # Front to back, the mirror image.
        s3 = seq((100, 2), (50, 2), (0, 2), (-50, 2), (-100, 1))
        self.assertEqual(self.events(s3 + [NONE, NONE, NONE]), ["press", "down", "release"])

    def test_release_follows_the_stroke_on_the_next_sample(self) -> None:
        s1 = seq((100, 2), (50, 2), (0, 2), (-50, 2), (-100, 1))
        out = self.run_samples(s1 + [NONE, NONE, NONE, NONE])
        # The 2nd empty sample confirms the release: the swipe, then the release.
        self.assertEqual(out[-4:], ["-", "down", "release", "-"])

    def test_short_pat_that_spread_on_landing_is_not_a_swipe(self) -> None:
        # Lands on the front, spreads back within 100 ms, rests 0.6 s, lifts.
        self.assertEqual(self.events(seq((100, 1), (50, 1), (-50, 12)) + [NONE, NONE, NONE]), ["press", "release"])

    def test_two_step_quick_touch_is_not_a_swipe(self) -> None:
        # A tap that rocks one step (e.g. -100 then 0) isn't a stroke.
        self.assertEqual(self.events(seq((-100, 2), (0, 2)) + [NONE, NONE]), ["press", "release"])

    def test_slow_drift_is_not_a_swipe(self) -> None:
        # From the K151 log: 75 to -50 over 3.3 s while resting. 50 per second here.
        drift = [0x34] * 20 + [0x3C] * 20 + [0x2E] * 20 + [0x0F] * 20   # 75, 50, 0, -50
        self.assertEqual(self.events(drift + [NONE, NONE]), ["press", "release"])

    def test_small_movement_is_not_a_swipe(self) -> None:
        # Settled at 0 (Z1), then +50 (Z1 and Z2 equal) stays under 60.
        self.assertEqual(self.events([Z1] * 4 + [Z1 | Z2] * 6 + [NONE, NONE]), ["press", "release"])

    def test_short_touch_during_landing(self) -> None:
        self.assertEqual(self.events([Z0, Z0, Z2, NONE, NONE]), ["press", "release"])

    def test_noise_dropout_during_confirm_still_swipes(self) -> None:
        # A single empty sample doesn't release (debounce) and doesn't count as held.
        self.assertEqual(self.events([Z0] * 4 + [Z2, Z2, NONE, Z2, Z2, NONE, NONE]),
                         ["press", "up", "release"])

    def test_upper_bits_ignored(self) -> None:
        self.assertEqual(self.events([0xC0, 0xC0, 0xC0]), [])


if __name__ == "__main__":
    unittest.main()
