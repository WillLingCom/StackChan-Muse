#!/usr/bin/env python3
"""Records N seconds of raw stereo (both mic slots) on the K151 ('>mic.raw=N') into private/audio/NAME.wav.
For checking the mics and calibrating direction finding; the file stays on this machine.
  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python scripts/k151_mic.py NAME [SECONDS]"""
import base64, glob, json, sys, time, wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "firmware" / "esp32" / "tools" / "muse"))
import chat  # noqa: E402

name, secs = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3
parts, meta = {}, None
with chat.Board(glob.glob("/dev/cu.usbmodem*")[0]) as b:
    b._keep_lines_on_close()
    b.write_line(f"mic.raw={secs}")
    deadline = time.monotonic() + secs + 30
    while meta is None:
        line = b.read_line(deadline)
        if line is None:
            break
        if "@mic.part {" in line:
            p = json.loads(line[line.index("{"):])
            parts[p["at"]] = p["b64"]
        elif "@mic {" in line:
            meta = json.loads(line[line.index("{"):])
data = b"".join(base64.b64decode(parts[k]) for k in sorted(parts))
out = ROOT / "private" / "audio" / f"{name}.wav"
with wave.open(str(out), "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(16000)
    w.writeframes(data)
print(json.dumps(meta), f"-> {out} ({len(data) // 4} frames)")
