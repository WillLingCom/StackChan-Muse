#!/usr/bin/env python3
"""Check publication files without printing secret values. No third-party packages."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEV_KEY_PATH = "firmware/esp32/dev_signing_key.pem"
DEV_KEY_HASH = "e57311281ea0478a80be57036768ef71481804eab73182b2b70255311a2ded6a"
FIXTURE_PATH = "firmware/esp32/tests/link_pairing_handshake_harness.c"
FIXTURE_HASH = "8669cd5d24a3a7f8a6a79cd42c0313cf39d2b27a1438cef0b53457244bce72be"
AUDIO_FIXTURE_PATH = "firmware/esp32/components/muse/test_reply.mp3"
AUDIO_FIXTURE_HASH = "d219062cdbf9ed59b149266653c2ba0e0f8517840de68272c7bdf5f9bedbc10f"
PATTERNS = {
    "Muse token": rb"mgst_[A-Za-z0-9_-]{43}(?![A-Za-z0-9_-])",
    "API key": rb"\bsk-[A-Za-z0-9_-]{24,}",
    "JWT": rb"eyJ[A-Za-z0-9_-]{16,}\.[A-Za-z0-9_-]{16,}\.[A-Za-z0-9_-]{16,}",
    "GitHub token": rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})",
    "private key": rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
}
CONFIG = re.compile(
    rb'^\s*(?:#\s*)?(CONFIG_\w*(?:TOKEN|KEY|PASSWORD|SSID))\s*=\s*"([^"\r\n]+)"',
    re.MULTILINE,
)
PRIVATE_DIRS = {"private", "backups", "managed_components", "__pycache__", ".venv"}
PRIVATE_SUFFIXES = {".bin", ".elf", ".map", ".log", ".wav", ".mp3", ".pcm", ".raw", ".dump"}
PRIVATE_DIRS.update({".audit", ".cache", ".claude", ".codex"})
EXCLUDED_ASSETS = {
    "firmware/esp32/avatar/muse_pixel.c", "firmware/esp32/avatar/jollybot.gif",
    "firmware/esp32/avatar/happy_anim.c", "firmware/esp32/avatar/happy_anim.h",
    "firmware/.github/images/muse-gadgets-dark.png",
    "firmware/.github/images/muse-gadgets-light.png",
}


def forbidden(path):
    p = Path(path)
    if path in EXCLUDED_ASSETS or path.startswith("firmware/esp32/components/muse/avatar/"):
        return True
    if any(part in PRIVATE_DIRS or part == "build" or part.startswith("build-") for part in p.parts):
        return True
    return (
        p.name in {"sdkconfig", "sdkconfig.old", ".sdk-token", ".env"}
        or p.name.startswith(".env.") and p.name != ".env.example"
        or p.name.endswith((".local", ".local.cfg", ".bin.local"))
        or p.suffix in PRIVATE_SUFFIXES
        or p.suffix in {".pem", ".key"} and path != DEV_KEY_PATH
    )


def placeholder(value):
    return value in {b"REPLACE_ME", b"mgst_REPLACE_ME"}


def scan(path, data, known):
    issues = []
    is_upstream_audio = path == AUDIO_FIXTURE_PATH and hashlib.sha256(data).hexdigest() == AUDIO_FIXTURE_HASH
    if forbidden(path) and not is_upstream_audio:
        issues.append((0, "private/generated file"))
    for name, value in known.items():
        offset = data.find(value)
        if offset >= 0:
            issues.append((data.count(b"\n", 0, offset) + 1, "known credential: " + name))
    for label, pattern in PATTERNS.items():
        for match in re.finditer(pattern, data):
            if label == "private key" and path == DEV_KEY_PATH and hashlib.sha256(data).hexdigest() == DEV_KEY_HASH:
                continue
            if label == "Muse token" and path == FIXTURE_PATH and hashlib.sha256(match[0]).hexdigest() == FIXTURE_HASH:
                continue
            issues.append((data.count(b"\n", 0, match.start()) + 1, label))
    for match in CONFIG.finditer(data):
        name, value = match.groups()
        if placeholder(value):
            continue
        if name == b"CONFIG_SECURE_BOOT_SIGNING_KEY" and value == b"dev_signing_key.pem":
            continue
        issues.append((data.count(b"\n", 0, match.start()) + 1, "non-placeholder " + name.decode()))
    for match in re.finditer(rb'(?:/Use' rb'rs/[^/\s]+/|[A-Z]:\\Users\\[^\\\s]+\\)', data):
        issues.append((data.count(b"\n", 0, match.start()) + 1, "personal absolute path"))
    return sorted(set(issues))


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true", help="Check the entire Git index, not working files")
    parser.add_argument("--history", action="store_true", help="Also inspect every reachable commit tree and commit/tag metadata")
    parser.add_argument("--private-config", type=Path, help="Read known secrets locally for exact matching; never prints them")
    args = parser.parse_args()
    known = {}
    if args.private_config:
        for match in CONFIG.finditer(args.private_config.read_bytes()):
            name, value = match.groups()
            if not placeholder(value) and len(value) >= 5 and name != b"CONFIG_SECURE_BOOT_SIGNING_KEY":
                known[name.decode()] = value
    if Path(os.fsdecode(git("rev-parse", "--show-toplevel")).strip()).resolve() != ROOT:
        sys.exit("FAIL: Git root must be this publication directory")
    if args.staged:
        names = [os.fsdecode(name) for name in git("ls-files", "-z").split(b"\0") if name]
        if not names:
            sys.exit("FAIL: the Git index is empty; run git add first")
        files = ((name, git("show", ":" + name)) for name in names)
    else:
        # Scan publication candidates: tracked files, plus unignored new files.
        # Ignored private/build caches are not upload candidates; --staged still
        # checks force-added ignored files. Neither mode reads a symlink target.
        paths = []
        for name in sorted(set(git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split(b"\0"))):
            if not name: continue
            p = ROOT / os.fsdecode(name)
            if p.is_symlink():
                sys.exit("FAIL: review symlink: " + str(p.relative_to(ROOT)))
            if p.exists(): paths.append(p)
        files = ((p.relative_to(ROOT).as_posix(), p.read_bytes()) for p in sorted(paths))
    count = failures = 0
    for path, data in files:
        count += 1
        for line, label in scan(path, data, known):
            print(f"FAIL {path}:{line}: {label}")
            failures += 1
    # Reject symlinks and submodules in the proposed publication tree.
    for entry in git("ls-files", "--stage", "-z").split(b"\0"):
        if entry and entry.split(b" ", 1)[0] in {b"120000", b"160000"}:
            print("FAIL: staged symlink/submodule requires separate review")
            failures += 1
    if args.history:
        commits = git("rev-list", "--all").splitlines()
        seen = set()
        for commit in commits:
            for line, label in scan("commit-metadata", git("cat-file", "commit", commit.decode()), known):
                print(f"FAIL commit metadata:{line}: {label}")
                failures += 1
            for entry in git("ls-tree", "-rz", commit.decode()).split(b"\0"):
                if not entry: continue
                metadata, path = entry.split(b"\t", 1)
                mode, kind, oid = metadata.split()
                name = os.fsdecode(path)
                if mode in {b"120000", b"160000"}:
                    print(f"FAIL history {name}: symlink/submodule")
                    failures += 1
                    continue
                if kind != b"blob" or (name, oid) in seen: continue
                seen.add((name, oid))
                for line, label in scan(name, git("cat-file", "blob", oid.decode()), known):
                    print(f"FAIL history {name}:{line}: {label}")
                    failures += 1
        for oid in git("for-each-ref", "--format=%(objectname)", "refs/tags").splitlines():
            if git("cat-file", "-t", oid.decode()).strip() == b"tag":
                for line, label in scan("tag-metadata", git("cat-file", "tag", oid.decode()), known):
                    print(f"FAIL tag metadata:{line}: {label}")
                    failures += 1
        print(f"History: {len(commits)} reachable commits, {len(seen)} distinct path/blob pairs.")
    if failures:
        print(f"FAILED: {failures} findings in {count} files. No secret values were printed.")
        return 1
    print(f"PASS: {count} files checked; no matches for configured credential/path rules.")
    print("Ignored local caches are outside this publication scan. Detection is rule-based, not a guarantee.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
