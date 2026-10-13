# StackChan × Muse

An open-source Physical AI robot experiment connecting StackChan hardware with Muse.

Bringing Muse AI into a physical robot — voice, vision, motion, and interactive behaviors.

This is a free, non-commercial community experiment for the M5Stack StackChan K151. It connects a physical robot's voice, touch, camera, motion and infrared interfaces to Muse. The firmware is based on the Muse Gadget SDK. Muse access and MiniMax speech synthesis require your own accounts and may incur service charges.

> [!IMPORTANT]
> **Source Preview.** This repository publishes the firmware source, robot
> control logic, Muse integration, configuration templates and development
> documentation for review. It is not yet a reproducible firmware release.
> The original Jollybot artwork and modified renderer are not included, and a
> clean standalone build of this publication has not been verified.

The rendering interfaces, animation callers, UI, expressions and existing CJK font are preserved. No replacement character is installed. See [asset preparation](docs/ASSET_PREPARATION.md). The missing visual assets remain **BLOCKED** for a complete build, but do not prevent publication of this source preview.

[Demo video — coming soon](docs/DEMO.md)

## Features and evidence

“Verified” below means recorded on the development K151 prototype in October 2026, not revalidated on this incomplete publication copy. See [validation and test scope](docs/VALIDATION.md).

| Status | Feature | What the records support |
| --- | --- | --- |
| Verified on prototype | Muse voice conversation | Press-to-talk, optional hands-free follow-up turns, displayed replies and MiniMax TTS. Touch/button interruption; no voice barge-in. |
| Verified on prototype | Touch and motion interactions | Screen/head touch, touch sounds, strokes, tilt, shake, lift and upside-down reactions. Original visuals await asset permission. |
| Verified on prototype | Camera-based visual Q&A | GC0308 320×240 stills attached to a device-started Muse chat turn. Successful object questions; a two-turn flow, roughly 13–19 seconds in the recorded final-path tests. |
| Verified on prototype | Robot motion and dance | Bounded yaw/pitch gestures, stop inputs, base RGB LEDs and two built-in synthesized tunes/dances (`happy`, `chill`). |
| Verified on prototype | Infrared control | Learn, store and send; NEC and raw captures. Muse tool flow and one TV tested, around 1 m on axis. |
| Experimental | Sound localization | Two-channel recordings, coarse near-field left/right estimation and optional turn-to-voice. No labelled accuracy study; no reliable far-field or front/back localization. |
| Experimental | Long-term operation | Prototype sessions and host tests exist; battery operation and multi-hour endurance are unverified. |
| Not yet implemented | Additional capabilities | Wake word, acoustic echo cancellation, voice barge-in, proximity, NFC, microSD workflows and INA226 battery gauge. OTA remains disabled and no production update system is provided. |

## Hardware

- M5Stack StackChan **K151**, including its CoreS3 head and robot body. A standalone CoreS3 is not equivalent to the complete robot.
- ESP32-S3, 16 MB flash, 8 MB quad PSRAM; 320×240 display and touch panel.
- ES7210 microphone ADC, dual microphone ports, AW88298 speaker amplifier and speaker.
- GC0308 camera, BMI270 IMU, Si12T head touch and PY32 body controller.
- Two SCSCL bus servos, base RGB LEDs and the head-top IR transmitter/receiver.
- USB data cable and suitable power; a phone with the Muse app and Wi-Fi.

See [hardware mapping](HARDWARE_MAP.md). Servo zero settings in the board overlay are prototype calibration values, **not universal calibration**. Check your unit before enabling motion.

## Repository layout

```text
CONFIG.example                   # placeholders only
LICENSE                          # Apache-2.0; exceptions below
LICENSES/                        # preserved third-party notices
THIRD_PARTY_NOTICES.md
firmware/
  LICENSE                        # original Muse SDK license
  esp32/
    components/muse/boards/       # StackChan drivers and tools
    components/muse/fonts/        # original CJK fallback, unchanged
    main/                        # Muse transport, pairing and application
    devices/                     # board overlays + credential-free overlay
    avatar/                      # documentation only; artwork excluded
    dependencies.lock            # pinned prototype dependency baseline
    sdkconfig.defaults
    partitions*.csv
    tests/  simulator/  tools/
  linux/  skills/                 # retained upstream SDK material
muse_skill/SKILL.md               # teach Muse the robot's tools
scripts/                         # portable build, config and audit helpers
docs/                            # asset status, validation and change inventory
```

Upstream profiles and Linux utilities are retained for provenance; this project's hardware evidence concerns K151 only. Local `private/`, `.cache/`, `.audit/`, generated SDK configuration, managed components, logs and firmware images are ignored.

## Environment and dependency preparation

Use **ESP-IDF v6.0.1** with the ESP32-S3 toolchain, Python supported by that IDF release, Git, CMake and Ninja. Follow the [official ESP-IDF v6.0.1 installation guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html) and activate its `export.sh` in your terminal. Alternatively set `IDF_EXPORT` to your own installation's `export.sh`. No developer-specific installation path is required.

ESP-IDF Component Manager fetches registry dependencies during the build. JPEG is pinned to **1.0.2**, CoreS3 BSP to **4.1.0**, and LVGL to **9.5.0**. The lock file records all resolved prototype versions and hashes. Direct manifests were tightened for publication; clean resolution with those edits is still unverified. The build helper checks that resolution does not change the recorded dependency versions or hashes.

There is currently **no cleared one-command download of the original visual assets**. The official personal-avatar generator makes a new character and needs a working paired board; it is not a reproducible bootstrap for the original visuals. Do not treat fetching artwork into an ignored directory as permission to use it.

## Build and flash

From this repository's root, the credential-free build entry point is:

```sh
scripts/k151.sh build-public
```

**In this Source Preview, this exits with `BLOCKED` before building**, because the original artwork is excluded. It does not download artwork, replace the avatar or flash a board. Once asset permission and packaging are resolved, the same entry point is intended to prepare ordinary registry dependencies and build in `firmware/esp32/build-k151-public/`, using only public defaults.

For a future authorized device build, keep credentials in an ignored local overlay:

```sh
mkdir -p private
cp CONFIG.example private/k151.local.cfg
chmod 600 private/k151.local.cfg
# Edit locally, or enter secrets without echoing them:
scripts/set_token.sh
scripts/set_tts_key.sh
scripts/k151.sh reconfig
```

These are post-unblocking instructions. `reconfig` regenerates only the development build's SDK configuration; never add `private/` or build output to Git. Wi-Fi can be provisioned by BLE instead of compiling credentials. The public build never reads this overlay.

After a successful authorized build, explicitly identify your board and port:

```sh
# Replace the placeholders locally; never commit a real device identity.
export K151_MAC='<YOUR_DEVICE_MAC>'
scripts/k151.sh flash '<SERIAL_PORT>'
scripts/k151.sh monitor '<SERIAL_PORT>' 30
```

Flashing replaces the ESP32 application/bootloader/partition layout. Back up your own device first. The helper checks the device identity and refuses development configurations that enable secure boot, flash/NVS encryption, eFuse pairing authentication or OTA. No flashing was performed during publication preparation.

## Pairing with Muse

After an authorized build is flashed:

1. Obtain your own SDK token from [Muse Gadgets](https://gadgets.muse.ai/) and review the [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms). The token belongs in the private overlay.
2. In the Muse app, open **Settings → Devices**, enable Developer mode, select your own advertised `MuseGadget` device and follow BLE Wi-Fi setup and physical confirmation prompts. App flow and availability depend on Muse.
3. Give Muse the instructions in [muse_skill/SKILL.md](muse_skill/SKILL.md), selecting your own paired robot. No prototype identifier is included.
4. Configure your own MiniMax key for spoken replies. The key is compiled into the development image, so keep that image private. Touch/button interaction starts conversations; there is no wake word.

## Validation and limitations

Run host tests after obtaining the ordinary cJSON dependency:

```sh
scripts/k151.sh test
python3 scripts/check_public.py --history
python3 scripts/check_public.py --staged --history
```

The publication preparation host tests used cached public cJSON sources. This does not establish a clean firmware build, simulator render or hardware regression. The original font bytes are retained; no font regeneration or visual comparison was performed. See [validation](docs/VALIDATION.md) for exact results and skipped checks.

Known limits include half-duplex audio, energy-based voice detection, small camera memory margins and one unexplained historical reboot, limited servo travel, unreliable far-field direction estimates, and one-way short-range IR. The head-touch auto-nod behavior remains disabled. See [known limitations](KNOWN_LIMITATIONS.md) and [IR details](IR_CAPABILITIES.md).

## Security and licenses

Never publish SDK tokens, TTS keys, Wi-Fi credentials, paired-device state, flash dumps or private logs. Secrets compiled into a binary can be extracted; this is a development experiment, not a secure product credential-distribution scheme. Rotate any credential that was previously disclosed.

The SDK's public `dev_signing_key.pem` is intentionally retained as an exact upstream development fixture, not a private production key. Do not use it as a production trust root. The publication scanner permits only specific unchanged upstream fixtures and scans the proposed index and reachable history without printing secret values.

The project's free/non-commercial purpose does not change the rights granted by its source licenses. Source marked Apache-2.0 uses [LICENSE](LICENSE); third-party code and fonts retain their own licenses. The retained CJK fallback is the OFL-licensed GNU Unifont subset; no proprietary font is included. GNU Unifont's OFL terms and Espressif's hardware-use restrictions are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No blanket MIT relicensing is applied. Jollybot and related images are not distributed here.

## Deotaland Physical Agent Platform

[Deotaland](https://github.com/Deotaland) and [Agent Link](https://github.com/Deotaland/Agent_link) are related ecosystem projects for physical agents and embedded-device integration. This firmware uses the **Muse Gadget SDK** and does **not** incorporate the Agent_link SDK. No Agent Link compatibility, integration or platform endorsement is claimed.
