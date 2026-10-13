#!/usr/bin/env python3
"""Offline readiness check; never downloads or imports license-excluded artwork."""
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    manifest = json.loads((ROOT / "docs/assets-status.json").read_text())
    missing = []
    mismatched = []
    for entry in manifest["assets"]:
        path = ROOT / entry["path"]
        if not path.is_file():
            missing.append(entry["path"])
        elif hashlib.sha256(path.read_bytes()).hexdigest() != entry["prototype_sha256"]:
            mismatched.append(entry["path"])
    if manifest["status"] == "BLOCKED" or missing or mismatched:
        print("BLOCKED: original visual assets are not cleared for this publication.")
        print(f"Missing assets: {len(missing)}; different from prototype: {len(mismatched)}.")
        print("See docs/ASSET_PREPARATION.md. No substitute or automatic download is enabled.")
        return 2
    print("Asset hashes match the prototype; this check does not grant usage rights.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
