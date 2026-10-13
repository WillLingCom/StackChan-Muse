# Validation record

Publication preparation was performed on 2026-10-13. No board was flashed and
no credential was used by a build or test.

## Publication-copy checks

| Check | Result | Scope |
| --- | --- | --- |
| Host unit tests | PASS: 250 tests, 3 skipped | Used the locked public cJSON source from an existing local ESP-IDF component cache and ESP-IDF 6.0.1 headers. It did not use the original private project directory. |
| Credential/path scan | PASS: 622 candidate files before final staging | Compared against the known local credential values without printing them and checked token/key patterns, private/generated paths and personal absolute paths. Final staged and history scans are recorded in `PUBLICATION_CHANGES.md`. |
| Original CJK font | Unchanged | Byte-for-byte equal to the publication baseline. Per user direction, it was not regenerated or visually rechecked. Upstream license documents were added separately. |
| Asset readiness | BLOCKED | Four original visual files are deliberately absent because permission is unresolved. `scripts/check_assets.py` reports this before a firmware build. |
| Clean ESP-IDF build | NOT COMPLETED | A first sandboxed attempt was blocked by macOS process-query permissions. After the asset gate was added, the supported command correctly stops at the unresolved asset issue. No successful build is claimed. |
| Simulator render | NOT RUN | It requires the excluded renderer and therefore cannot validate the original visual output in this publication copy. |
| Hardware regression | NOT RUN | Publication preparation did not flash or operate a device. |

The three skipped checks require the optional Python `cryptography` package,
host PSA Crypto development headers/libraries, or excluded private direction
calibration recordings. The normal helper also passed 250 tests with four
skips when `IDF_PATH` was not exported; the extra skipped case was the real
Mbed TLS pairing harness. The passing suite covers the public logic available
in this checkout, including StackChan gestures, dance, infrared, motion,
camera, voice/VAD and Muse transport harnesses. Passing host tests do not
establish electrical or mechanical safety on another robot.

## Historical prototype evidence

The private development test record, logs, photos, audio and flash artifacts
were intentionally excluded. The following summary is the claim boundary used
by the public README:

- Voice conversation, touch interaction, motion reactions, camera-to-Muse
  visual Q&A, bounded robot movement, both built-in dances and infrared
  learn/send were exercised end to end on one K151 prototype.
- Coarse sound direction was acceptable in an unlabelled device session, but
  far-field measurements overlapped and no labelled accuracy rate exists. It
  remains experimental.
- Camera Q&A works by sending the photo in a second, device-started chat turn;
  direct JPEG data inside a tool result was not usable by the platform.
- No wake word, acoustic echo cancellation, voice barge-in, proximity, NFC,
  microSD workflow, INA226 battery gauge or production OTA system exists.
- Battery operation, multi-hour endurance and the publication copy's missing
  visuals were not verified.

The source itself and the public documents contain further constraints in
`KNOWN_LIMITATIONS.md`, `HARDWARE_MAP.md`, `DANCE_FEASIBILITY.md` and
`IR_CAPABILITIES.md`.
