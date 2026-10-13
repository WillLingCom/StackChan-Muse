# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 infrared codes (components/muse/boards/stackchan_ir_codec.c): NEC frames are recognised within
tolerance and re-generated cleanly, other remotes are kept raw, junk and out-of-range timing are refused,
names are checked, and the storage form round-trips and rejects damage."""

from __future__ import annotations

import os
import random
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def nec_timing(code: int, jitter: float = 0.0, rng: random.Random | None = None) -> list[int]:
    rng = rng or random.Random(1)
    j = lambda v: int(v * (1 + rng.uniform(-jitter, jitter)))
    us = [j(9000), j(4500)]
    for i in range(32):
        us += [j(560), j(1690 if code >> i & 1 else 560)]
    return us + [j(560)]


class IrCodecTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "ir"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        str(ROOT / "tests" / "stackchan_ir_codec_harness.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_ir_codec.c"), "-o", str(cls.exe)],
                       check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_lines(self, *lines: str) -> list[str]:
        out = subprocess.run([str(self.exe)], input="\n".join(lines) + "\n", capture_output=True, text=True,
                             check=True).stdout.splitlines()
        self.assertEqual(len(out), len(lines))
        return out

    def capture(self, us: list[int]) -> str:
        return "capture " + " ".join(map(str, us))

    def test_nec_is_recognised_with_jitter_and_a_trailing_gap(self) -> None:
        rng = random.Random(7)
        codes = [0x00FF00FF, 0xF20D7F80, 0x12345678, 0x0]
        lines = [self.capture(nec_timing(c, 0.15, rng)) for c in codes]
        lines.append(self.capture(nec_timing(0xF20D7F80) + [20000]))   # a gap before silence
        out = self.run_lines(*lines)
        for c, o in zip(codes + [0xF20D7F80], out):
            self.assertEqual(o, f"nec {c:08x} packed 13 roundtrip ok truncated refused")

    def test_repeats_after_a_gap_are_dropped(self) -> None:
        frame = [2600, 900] + [450, 450, 450, 900] * 20 + [450]          # a non-NEC frame
        three = frame + [12000] + frame + [12000] + frame                   # one press, three frames
        nec_rep = nec_timing(0x00FF00FF) + [9500] + nec_timing(0x00FF00FF)
        out = self.run_lines(self.capture(three), self.capture(nec_rep))
        self.assertTrue(out[0].startswith(f"raw {len(frame)} x3 gap 12000 packed {13 + 2 * len(frame)} roundtrip ok"),
                        out[0])
        self.assertEqual(out[1], "nec 00ff00ff packed 13 roundtrip ok truncated refused")   # NEC: sent once
        five = self.run_lines(self.capture((frame + [12000]) * 4 + frame), self.capture(frame + [9000] + frame))
        self.assertIn(" x3 gap 12000 ", five[0])                                             # capped at 3
        self.assertIn(" x2 gap 9000 ", five[1])

    def test_raw_timing_loses_the_receivers_mark_excess(self) -> None:
        frame = [1100, 520] + [680, 520, 680, 1090] * 5 + [680]          # as a receiver reports it
        exe_out = subprocess.run([str(self.exe)], input=self.capture(frame) + "\n", capture_output=True, text=True,
                                 check=True, env={**os.environ, "IR_DUMP": "1"}).stdout
        us = list(map(int, exe_out.split(" us ")[1].split()[0].split(",")))
        self.assertEqual(us, [v - 90 if i % 2 == 0 else v + 90 for i, v in enumerate(frame)])   # marks -90, spaces +90

    def test_encode_decode_roundtrip(self) -> None:
        for o in self.run_lines("nec 00ff00ff", "nec f20d7f80", "nec ffffffff"):
            n, ok, _ = o.split()
            self.assertEqual((n, ok), ("67", "ok"))

    def test_other_remotes_are_kept_raw(self) -> None:
        sony = [2400, 600] + [1200 if i % 3 else 600 for i in range(24)] + [600]   # 12-bit SIRC-like
        sony = sony[:2] + [v for pair in zip(sony[2:26:2], [600] * 12) for v in pair] + [600]
        out = self.run_lines(self.capture(sony), self.capture(nec_timing(0xF20D7F80, 0.4, random.Random(3))))
        self.assertTrue(out[0].startswith(f"raw {len(sony)} packed {13 + 2 * len(sony)} roundtrip ok"), out[0])
        self.assertTrue(out[0].endswith("truncated refused"))
        self.assertTrue(out[1].startswith("raw "), out[1])   # too far off for NEC: kept as captured

    def test_junk_is_refused(self) -> None:
        out = self.run_lines("capture", self.capture([500]), self.capture([500, 500]),
                             self.capture([500, 40, 500]), self.capture([500, 500, 31000]),
                             self.capture([600, 600] * 300 + [600]),
                             self.capture([7000, 7000] * 30 + [7000]))
        self.assertEqual(out, ["refused too short: not a remote's signal", "refused too short: not a remote's signal",
                               "refused too short: not a remote's signal", "refused duration out of range",
                               "refused duration out of range", "refused too long to keep", "refused frame too long"])

    def test_names(self) -> None:
        self.assertEqual(self.run_lines("name 电视开关", "name tv power", "name", "name " + "x" * 31,
                                        "name " + "x" * 32, "name a\tb"),
                         ["ok", "ok", "bad", "ok", "bad", "bad"])


if __name__ == "__main__":
    unittest.main()
