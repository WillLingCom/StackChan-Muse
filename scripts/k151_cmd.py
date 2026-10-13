#!/usr/bin/env python3
"""Sends one console command (">CMD") to the K151 and prints its "@KIND {...}" reply.
Uses the Muse tools' serial handling (tools/muse/chat.py), which doesn't reset the board.
  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python scripts/k151_cmd.py body [PORT]
K151_WAIT: seconds to wait for the reply; K151_TAIL: seconds to keep printing the log after it."""
import glob
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware" / "esp32" / "tools" / "muse"))
import chat  # noqa: E402

cmd = sys.argv[1]
ports = sys.argv[2:] or glob.glob("/dev/cu.usbmodem*")
if len(ports) != 1:
    sys.exit(f"need exactly one port, found {ports}")
with chat.Board(ports[0]) as b:
    b._keep_lines_on_close()
    b.write_line(cmd)
    kind = "@" + cmd.split("=")[0].split(".")[0]      # ">servo.nudge=..." answers "@servo"
    f = None
    deadline = time.monotonic() + float(__import__("os").environ.get("K151_WAIT", "15"))
    while f is None:                                  # every other line goes to stderr, for the log
        line = b.read_line(deadline)
        if line is None:
            break
        at = line.find(kind + " {")
        if at < 0:
            print(line, file=sys.stderr)
            continue
        f = json.loads(line[at + len(kind) + 1:])
    if f is None:
        sys.exit("no reply")
    print(json.dumps(f, ensure_ascii=False, indent=1))
    tail = time.monotonic() + float(__import__("os").environ.get("K151_TAIL", "0"))   # keep logging after it
    while (line := b.read_line(tail)) is not None:
        print(line, file=sys.stderr)
