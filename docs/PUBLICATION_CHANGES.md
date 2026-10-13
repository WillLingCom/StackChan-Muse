# Source Preview publication changes

Scope: only the independent `publish/MuseStackChan/` checkout. The original
development project and all upstream checkouts were left unchanged.

## Excluded from the public tree

- `firmware/esp32/avatar/muse_pixel.c`
- `firmware/esp32/avatar/jollybot.gif`
- `firmware/esp32/avatar/happy_anim.c`
- `firmware/esp32/avatar/happy_anim.h`
- `firmware/.github/images/muse-gadgets-dark.png`
- `firmware/.github/images/muse-gadgets-light.png`
- Every private config, token, TTS key, Wi-Fi credential, device backup,
  personal recording/photo, build output, generated `sdkconfig`, cache and log
- The private development test report and evidence directories

The first four paths remain as build-time interfaces/references because the
goal is to preserve the original behavior once permission is resolved. They
are also hard-excluded by `.gitignore` and `scripts/check_public.py`.

## Modified for publication

- `muse_skill/SKILL.md`: removed the prototype device identifier and made
  device selection generic.
- `.gitignore`: added private configuration, binary, cache, log, generated
  configuration and excluded-artwork rules while retaining required default
  overlays, partition tables and `dependencies.lock`.
- `CONFIG.example`: credential placeholders only.
- `scripts/k151.sh`: removed the local Python path, requires ESP-IDF v6.0.1,
  keeps caches/builds inside the checkout, provides a credential-free public
  profile, checks safety/asset/dependency gates, and requires explicit device
  identity and port before flashing.
- `scripts/set_token.sh`, `scripts/set_tts_key.sh`, `scripts/doa_offline.py`:
  portable paths and local-only secret/data handling.
- Component manifests: direct dependencies pinned for the K151 publication;
  JPEG is fixed at 1.0.2 and ESP-IDF at 6.0.1.
- `firmware/esp32/CMakeLists.txt`: fails with an explanatory message when the
  excluded original visual assets are unavailable.
- `firmware/esp32/tests/test_muse_serial_chat.py`: uses a non-rendering parser
  fixture instead of importing an excluded renderer into the public tests.
- Documentation: English README, pairing/build/security instructions, source
  provenance, validation scope, asset status and third-party notices.

## Added public support files

- Root `LICENSE` (Apache-2.0) and `LICENSES/` third-party texts
- `firmware/esp32/devices/sdkconfig.public`
- `firmware/esp32/dependencies.lock` and `docs/dependency-baseline.json`
- `scripts/check_assets.py`, `scripts/check_dependency_lock.py` and expanded
  `scripts/check_public.py`

No push, force push, flash or original-project history rewrite was performed.
The target repository's existing `Initial commit` was fetched read-only and
its README tagline was retained in the expanded Source Preview README.

## Final verification

- Proposed initial tree: 625 staged files; the six excluded artwork/banner
  paths are absent from the index.
- Remote baseline: `origin/main` at `65f2bf27de1ec4f8be6d974fd9356ea1419e7216`,
  containing only the two-line initial README when reviewed. The local Source
  Preview commit is prepared as its direct child for a future fast-forward
  push.
- Publication scanner: PASS on the 625-file staged tree; PASS on candidate
  working files; zero reachable commits/tags to scan. Known local credentials
  were used only for exact-match detection and were never printed.
- Git object cleanup: 26 unreachable objects from the earlier, uncommitted
  staging state were pruned; zero unreachable objects remain. The staged tree
  was verified unchanged by that cleanup.
- Host tests: PASS, 250 run and 3 skipped with ESP-IDF 6.0.1 headers available.
- Original development checkout, firmware worktree and Muse SDK upstream:
  unchanged from the pre-cleanup hash baseline.
- Clean firmware build and visual simulator: BLOCKED by the four deliberately
  excluded original visual files. No success is claimed. The repository is
  published as source preview material rather than a reproducible release.
