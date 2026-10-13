#!/usr/bin/env python3
"""Takes one photo on the K151 ('>camera') and saves the JPEG here (private/photos/), for checking the camera.
  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python scripts/k151_photo.py [NAME]"""
import base64, glob, json, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "firmware" / "esp32" / "tools" / "muse"))
import chat  # noqa: E402

name = sys.argv[1] if len(sys.argv) > 1 else time.strftime("photo_%H%M%S")
port = glob.glob("/dev/cu.usbmodem*")[0]
parts, result, log = {}, None, []
with chat.Board(port) as b:
    b._keep_lines_on_close()
    b.write_line("camera")
    deadline = time.monotonic() + 20
    while result is None:
        line = b.read_line(deadline)
        if line is None:
            break
        if "@camera.part {" in line:
            p = json.loads(line[line.index("{"):])
            parts[p["at"]] = p["b64"]
        elif "@tool {" in line:
            result = json.loads(line[line.index("{"):])
        elif "camera" in line:
            log.append(line)
print("\n".join(log[-8:]))
print(json.dumps(result, ensure_ascii=False))
if parts:
    data = base64.b64decode("".join(parts[k] for k in sorted(parts)))
    out = ROOT / "private" / "photos" / f"{name}.jpg"
    out.write_bytes(data)
    print(f"saved {out} ({len(data)} bytes)")
