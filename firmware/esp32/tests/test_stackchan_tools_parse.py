# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 the StackChan K151 Muse port authors.
"""robot.gesture params are checked before anything moves (boards/stackchan_tools_parse.c):
types, ranges, NaN/Inf-like values, unknown keys; repeated request ids are refused."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CJSON = ROOT / "managed_components" / "espressif__cjson" / "cJSON"


class ToolsParseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest("C compiler not available")
        if not (CJSON / "cJSON.c").exists():
            raise unittest.SkipTest("run one idf.py build first (managed_components/cJSON)")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "parse"
        subprocess.run([*cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "muse"),
                        "-I", str(CJSON), str(ROOT / "tests" / "stackchan_tools_parse_harness.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_tools_parse.c"),
                        str(ROOT / "components" / "muse" / "boards" / "stackchan_gesture.c"), str(CJSON / "cJSON.c"),
                        "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_lines(self, *lines: str) -> list[str]:
        out = subprocess.run([str(self.exe)], input="\n".join(lines) + "\n", capture_output=True, text=True,
                             check=True).stdout.splitlines()
        self.assertEqual(len(out), len(lines))
        return out

    def test_valid_calls(self) -> None:
        self.assertEqual(self.run_lines('{"gesture":"nod"}', '{"gesture":"shake","repeat":3,"speed":0.5}',
                                        '{"gesture":"look_left","speed":1}', '{"gesture":"home","repeat":1.0}'),
                         ["ok 0 1 1.00", "ok 1 3 0.50", "ok 3 1 1.00", "ok 5 1 1.00"])

    def test_refused_calls(self) -> None:
        out = self.run_lines(
            "null", "{}", '{"gesture":5}', '{"gesture":"dance"}', '{"gesture":"spin"}', '{"gesture":"NOD"}',
            '{"gesture":"nod","repeat":0}', '{"gesture":"nod","repeat":4}', '{"gesture":"nod","repeat":1.5}',
            '{"gesture":"nod","repeat":"2"}', '{"gesture":"nod","speed":2}', '{"gesture":"nod","speed":0.1}',
            '{"gesture":"nod","speed":1e999}', '{"gesture":"nod","speed":-1e999}', '{"gesture":"nod","repeat":1e999}',
            '{"gesture":"nod","angle":90}', '{"gesture":"nod","speed":null}', '{"gesture":"turn_left"}',
            '{"gesture":"turn_right"}')
        for line, o in zip(range(99), out):
            self.assertTrue(o.startswith("err invalid_params"), (line, o))

    def test_repeated_request_ids(self) -> None:
        self.assertEqual(self.run_lines("id:a", "id:b", "id:a", "id:c", "id:b", "id:"), ["new", "new", "dup", "new",
                                                                                          "dup", "new"])

    def test_old_ids_are_forgotten_after_16(self) -> None:
        lines = [f"id:r{i}" for i in range(17)] + ["id:r0", "id:r16"]
        self.assertEqual(self.run_lines(*lines)[-2:], ["new", "dup"])


if __name__ == "__main__":
    unittest.main()
