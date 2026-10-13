#!/usr/bin/env python3
"""Reject dependency version/hash/source drift from the recorded K151 baseline."""
# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import sys

def main():
    try:
        import yaml  # Supplied by the activated ESP-IDF environment.
    except ImportError:
        print("Activate ESP-IDF v6.0.1 before checking its dependency lock.", file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[1]
    expected = json.loads((root / "docs/dependency-baseline.json").read_text())
    lock = yaml.safe_load((root / "firmware/esp32/dependencies.lock").read_text())
    actual = {n: {k: v[k] for k in ("version", "component_hash", "source") if k in v}
              for n, v in lock["dependencies"].items()}
    if lock.get("target") != expected["target"] or actual != expected["dependencies"]:
        print("BLOCKED: dependency versions, content hashes or sources changed; review the lock.")
        return 2
    print(f"Dependency baseline matches: {len(actual)} entries. This is not a build validation.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
