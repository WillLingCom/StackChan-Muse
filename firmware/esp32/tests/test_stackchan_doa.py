# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 experimental talker direction (components/muse/boards/stackchan_doa.c): sub-sample delay on
synthetic fractional delays, sign (+ = the robot's right), gating, and decisions. If the private
calibration recordings exist on this machine (private/audio), also checks them."""

from __future__ import annotations

import os
import random
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path

try:
    import numpy as np
except ImportError:   # the synthetic signals need numpy
    np = None

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT.parents[1] / "private" / "audio"
RATE, FRAME = 16000, 320


def stereo(delay: float, seconds_quiet: float = 1.0, seconds_speech: float = 1.5, noise_db: float = -70.0,
           speech_db: float = -40.0, seed: int = 1) -> bytes:
    """Speech-like band noise reaching L first by `delay` samples (+ = L first = robot's right)."""
    rng = np.random.default_rng(seed)
    n_q, n_s = int(seconds_quiet * RATE), int(seconds_speech * RATE)
    s = rng.standard_normal(n_s)
    S = np.fft.rfft(s)
    f = np.fft.rfftfreq(n_s, 1 / RATE)
    S[(f < 300) | (f > 3400)] = 0
    env = 0.6 + 0.4 * np.sin(2 * np.pi * 4 * np.arange(n_s) / RATE)
    left = np.fft.irfft(S, n_s)
    right = np.fft.irfft(S * np.exp(-2j * np.pi * f * delay / RATE), n_s)   # R later by delay
    scale = 32768 * 10 ** (speech_db / 20) / np.sqrt(np.mean(left ** 2))
    left, right = left * env * scale, right * env * scale
    noise = lambda n: rng.standard_normal(n) * 32768 * 10 ** (noise_db / 20)
    L = np.concatenate((noise(n_q), left + noise(n_s), noise(n_q)))
    R = np.concatenate((noise(n_q), right + noise(n_s), noise(n_q)))
    x = np.clip(np.stack((L, R), 1), -32768, 32767).astype("<i2")
    return x.tobytes()


class DoaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if np is None:
            raise unittest.SkipTest("numpy not available")
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "doa"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        str(ROOT / "tests" / "stackchan_doa_harness.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_doa.c"), "-lm", "-o", str(cls.exe)],
                       check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_doa(self, pcm: bytes, begin: int, center: float = 0.0, thr: float = 0.1) -> tuple[str, int, float]:
        out = subprocess.run([str(self.exe), str(begin), str(center), str(thr)], input=pcm, capture_output=True,
                             check=True).stdout.split()
        return out[0].decode(), int(out[1]), float(out[2])

    def test_fractional_delays_are_measured(self) -> None:
        for d in (-1.2, -0.6, -0.25, 0.0, 0.25, 0.6, 1.2):
            side, frames, med = self.run_doa(stereo(d), begin=50)
            self.assertGreaterEqual(frames, 40, d)
            self.assertAlmostEqual(med, d, delta=0.12, msg=f"delay {d}")

    def test_sides(self) -> None:
        self.assertEqual(self.run_doa(stereo(+0.4), 50)[0], "right")    # L first = the robot's right
        self.assertEqual(self.run_doa(stereo(-0.4), 50)[0], "left")
        self.assertEqual(self.run_doa(stereo(0.03), 50)[0], "center")

    def test_center_offset_and_threshold(self) -> None:
        self.assertEqual(self.run_doa(stereo(-0.10), 50, center=-0.03, thr=0.10)[0], "center")
        self.assertEqual(self.run_doa(stereo(-0.20), 50, center=-0.03, thr=0.10)[0], "left")

    def test_noise_only_collects_nothing(self) -> None:
        side, frames, _ = self.run_doa(stereo(0.5, seconds_speech=0.02, speech_db=-90), begin=50)
        self.assertEqual((side, frames), ("unsure", 0))

    def test_too_few_frames_is_unsure(self) -> None:
        side, frames, _ = self.run_doa(stereo(0.6, seconds_speech=0.1), begin=50)
        self.assertEqual(side, "unsure")
        self.assertLess(frames, 8)

    def test_only_the_first_1_5_s_counts(self) -> None:
        side, frames, _ = self.run_doa(stereo(0.6, seconds_speech=4.0), begin=50)
        self.assertEqual(frames, 75)

    @unittest.skipUnless((PRIVATE / "cal_front_1m.wav").exists(), "private calibration recordings not on this machine")
    def test_private_calibration_recordings(self) -> None:
        def pcm(name: str) -> bytes:
            with wave.open(str(PRIVATE / f"{name}.wav")) as w:
                return w.readframes(w.getnframes())
        # The quiet recording's floor first, then the talk: begin after 1 s of quiet.
        quiet = pcm("quiet")
        for name, want in (("cal_front_1m", "center"), ("cal_left_1m", "left"), ("cal_left_30cm", "left")):
            side, frames, med = self.run_doa(quiet + pcm(name), begin=len(quiet) // 4 // FRAME, center=-0.03, thr=0.10)
            self.assertEqual(side, want, f"{name}: {side} {frames} {med:+.2f}")


if __name__ == "__main__":
    unittest.main()
