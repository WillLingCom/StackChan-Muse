# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""K151 reaction sounds (components/muse/boards/stackchan_sfx.c): each is short, audible, soft (well under full
scale), starts and ends quietly (no click), and renders the same in pieces as in one go."""

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
#include <stdlib.h>
#include "boards/stackchan_sfx.h"
int main(void) {
    static int16_t a[32000], b[32000];
    for (int s = 0; s < SFX_COUNT; s++) {
        size_t n = sfx_frames(s), got = sfx_render(s, 0, a, 32000), k = 0, m;
        while ((m = sfx_render(s, k, b + k, 77)) > 0) k += m;
        int peak = 0, same = got == k, first = abs(a[0]), last = got ? abs(a[got - 1]) : 0;
        for (size_t i = 0; i < got; i++) { if (abs(a[i]) > peak) peak = abs(a[i]); if (a[i] != b[i]) same = 0; }
        printf("%d %zu %zu %d %d %d %d\n", s, n, got, peak, first, last, same);
    }
    return 0;
}
"""


class SfxTest(unittest.TestCase):
    def test_sounds(self) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "s.c", Path(tmp) / "s"
            src.write_text(PROGRAM)
            subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                            str(src), str(ROOT / "components" / "muse" / "boards" / "stackchan_sfx.c"), "-lm",
                            "-o", str(exe)], check=True)
            rows = [list(map(int, l.split())) for l in subprocess.run([str(exe)], capture_output=True, text=True,
                                                                       check=True).stdout.splitlines()]
        self.assertEqual(len(rows), 7)
        for s, n, got, peak, first, last, same in rows:
            self.assertEqual(n, got)
            self.assertTrue(3200 <= n <= 19200, (s, n))     # 0.2-1.2 s
            self.assertTrue(3000 < peak <= 12000, (s, peak))  # audible, soft
            self.assertLess(first, 500)                       # no click in
            self.assertLess(last, 800)                        # nor out
            self.assertEqual(same, 1)                         # piecewise == whole


if __name__ == "__main__":
    unittest.main()
