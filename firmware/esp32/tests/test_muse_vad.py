# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""Hands-free turn detection (components/muse/muse_vad.c) on synthetic audio at the K151's measured levels
(noise floor about -72 dBFS, speech about -44 dBFS): starts, ends, short noises, pauses, rising noise."""

from __future__ import annotations

import math
import os
import random
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RATE, FRAME = 16000, 320


def segment(seconds: float, db: float, speech: bool, rng: random.Random) -> list[int]:
    """Noise at db dBFS RMS, or a speech-like signal (a voiced tone with syllable envelope) at db."""
    n = int(seconds * RATE)
    amp = 32768 * 10 ** (db / 20)
    out = []
    for i in range(n):
        if speech:
            env = 0.6 + 0.4 * math.sin(2 * math.pi * 4 * i / RATE)           # ~4 syllables a second
            x = env * (math.sin(2 * math.pi * 180 * i / RATE) + 0.5 * math.sin(2 * math.pi * 540 * i / RATE))
            x = x / 0.79 + rng.gauss(0, 0.05)
        else:
            x = rng.gauss(0, 1)
        out.append(max(-32768, min(32767, int(amp * x))))
    return out


class VadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "vad"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        str(ROOT / "tests" / "muse_vad_harness.c"), str(ROOT / "components" / "muse" / "muse_vad.c"),
                        "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def events(self, *parts: tuple[float, float, bool], idle_s: float = 0) -> list[tuple[float, str]]:
        rng = random.Random(1)
        pcm = []
        for secs, db, speech in parts:
            pcm += segment(secs, db, speech, rng)
        env = {**os.environ, "VAD_TRACK": str(int(idle_s * RATE / FRAME))}
        out = subprocess.run([str(self.exe)], input=struct.pack(f"<{len(pcm)}h", *pcm), capture_output=True,
                             check=True, env=env).stdout.decode().split()
        return [(int(out[i]) * FRAME / RATE, out[i + 1]) for i in range(0, len(out), 2)]

    NOISE = -72.0
    SPEECH = -44.0

    def test_tv_on_idle_floor_then_a_turn_ends(self) -> None:
        # A TV at -45 dBFS heard while idle (5 s), then a tap: 1 s TV, 2 s speech at -25, TV again.
        ev = self.events((5, -45, False), (1, -45, False), (2, -25, True), (3, -45, False), idle_s=5)
        self.assertEqual([e for _, e in ev], ["start", "end"])
        self.assertAlmostEqual(ev[0][0], 6.0, delta=0.3)
        self.assertLess(ev[1][0], 8.0 + 1.3)          # ends ~0.8 s after the voice stops

    def test_room_got_loud_without_idle_tracking_still_ends(self) -> None:
        # The floor is stale (-72) and a TV is on: the TV may open a turn, but after speech it ends.
        ev = self.events((1, -45, False), (2, -25, True), (3, -45, False))
        self.assertIn("end", [e for _, e in ev])
        self.assertLess(ev[-1][0], 3.0 + 1.3)

    def test_noise_alone_never_starts(self) -> None:
        self.assertEqual(self.events((10, self.NOISE, False)), [])

    def test_one_sentence(self) -> None:
        ev = self.events((1.0, self.NOISE, False), (1.5, self.SPEECH, True), (1.5, self.NOISE, False))
        self.assertEqual([e for _, e in ev], ["start", "end"])
        self.assertAlmostEqual(ev[0][0], 1.0 + 0.08, delta=0.06)    # ~100 ms after the onset
        self.assertAlmostEqual(ev[1][0], 2.5 + 0.78, delta=0.06)    # ~800 ms after the last word

    def test_click_is_ignored(self) -> None:
        self.assertEqual(self.events((1, self.NOISE, False), (0.06, -30, False), (2, self.NOISE, False)), [])

    def test_short_noise_is_not_a_turn(self) -> None:
        ev = self.events((1, self.NOISE, False), (0.2, self.SPEECH, True), (2, self.NOISE, False))
        self.assertEqual([e for _, e in ev], ["start", "noise"])

    def test_pause_inside_a_sentence_keeps_one_turn(self) -> None:
        ev = self.events((1, self.NOISE, False), (1.0, self.SPEECH, True), (0.5, self.NOISE, False),
                         (1.0, self.SPEECH, True), (1.5, self.NOISE, False))
        self.assertEqual([e for _, e in ev], ["start", "end"])

    def test_two_sentences_are_two_turns(self) -> None:
        ev = self.events((1, self.NOISE, False), (1.0, self.SPEECH, True), (1.5, self.NOISE, False),
                         (1.0, self.SPEECH, True), (1.5, self.NOISE, False))
        self.assertEqual([e for _, e in ev], ["start", "end", "start", "end"])

    def test_slowly_rising_noise_does_not_start(self) -> None:
        parts = [(0.5, self.NOISE + i, False) for i in range(0, 20)]   # -72 to -53 dBFS over 10 s, like a fan
        self.assertEqual(self.events(*parts), [])

    def test_quiet_room_soft_voice_still_starts(self) -> None:
        ev = self.events((1, -80, False), (1.2, -54, True), (1.2, -80, False))
        self.assertEqual([e for _, e in ev], ["start", "end"])


if __name__ == "__main__":
    unittest.main()
