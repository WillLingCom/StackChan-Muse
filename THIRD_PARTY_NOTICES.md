# Third-party notices

This repository does not place every component under one license. Each item
below keeps its own copyright and license terms. The copies under `LICENSES/`
are provided for review and redistribution compliance; the files fetched by
ESP-IDF Component Manager remain governed by their upstream notices.

## Source included in this repository

| Component | Source | License and conditions |
| --- | --- | --- |
| Muse Gadget SDK | [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk), base `74a5e2d7fc895f109f83a9a1dbed705dbcd8b1ff` | Apache-2.0, preserved as `firmware/LICENSE`. Upstream explicitly excludes Jollybot; that artwork is absent here. |
| StackChan hardware reference code | [m5stack/StackChan](https://github.com/m5stack/StackChan), `1b5765599fba8aaad1811d9a79358ccc7051f5f3` | MIT; retain its copyright and permission notice. See `LICENSES/M5Stack-StackChan-MIT.txt`. Register maps, initialization sequences and behavior are attributed in the derived source. |
| StackChan BSP reference code | [m5stack/StackChan-BSP](https://github.com/m5stack/StackChan-BSP), `8d4d6fc3b7a6be379c6317c45a02a30bff8c492e` | MIT; retain its copyright and permission notice. See `LICENSES/M5Stack-StackChan-BSP-MIT.txt`. |
| Stack-chan reference project | [stack-chan/stack-chan](https://github.com/stack-chan/stack-chan), `2f6b5a65e30278fdbd1c5114cab6d42cdb7b7a0d` | Apache-2.0. It is a hardware/behavior reference and is not vendored as a separate component. |
| minimp3 | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0; see `firmware/esp32/components/minimp3/LICENSE`. |
| Adafruit GFX pixel font | [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD-2-Clause; the full notice remains in `firmware/esp32/main/pixel_font.c`. |
| GNU Unifont 16.0.04 CJK subset | [GNU Unifont](https://unifoundry.com/unifont/) | The existing generated font is retained unchanged. The upstream compiled font is dual-licensed under SIL OFL-1.1 or GPL-2.0-or-later with the font embedding exception. This publication relies on OFL-1.1: keep the copyright notice and OFL with redistributed or modified font software, do not sell the font by itself, do not use reserved font names for a modified font without permission, and do not use contributor names for endorsement. See `LICENSES/Unifont-*`. |
| Muse SDK test reply audio | Muse Gadget SDK fixture | Retained unchanged from the Apache-2.0 upstream snapshot. It is used only by the upstream self-test. |
| Development signing key | Muse Gadget SDK fixture | Retained unchanged from upstream. It is public development material and provides no production trust or secrecy. |

The source added for this project uses its file-level SPDX declarations and
the root Apache-2.0 license. The MIT reference notices above are preserved; the
project does not relicense third-party work as MIT or Apache-2.0.

## ESP-IDF and managed build dependencies

The target toolchain is [ESP-IDF 6.0.1](https://github.com/espressif/esp-idf/releases/tag/v6.0.1), licensed primarily under Apache-2.0 with component-level exceptions. The exact K151 resolution, registry sources and content hashes are recorded in `firmware/esp32/dependencies.lock` and `docs/dependency-baseline.json`.

Apache-2.0 components from the [Espressif Component Registry](https://components.espressif.com/) are:

- `espressif/bmi270` 1.1.0
- `espressif/cmake_utilities` 0.5.3
- `espressif/esp_cam_sensor` 2.0.1
- `espressif/esp_codec_dev` 1.5.11
- `espressif/esp_io_expander` 1.2.1
- `espressif/esp_io_expander_aw9523` 1.0.0
- `espressif/esp_lcd_ili9341` 2.1.0
- `espressif/esp_lcd_touch` 1.2.1
- `espressif/esp_lcd_touch_ft5x06` 1.1.1
- `espressif/esp_lvgl_port` 2.9.0
- `espressif/esp_sccb_intf` 0.0.9
- `espressif/esp_websocket_client` 1.2.3
- `espressif/i2c_bus` 1.5.2
- `espressif/led_strip` 3.0.3
- `espressif/m5stack_core_s3` 4.1.0
- `espressif/sensor_hub` 0.1.5
- `espressif/usb` 1.6.0
- `espressif/usb_host_uvc` 2.4.2

Other locked managed components are:

| Component | Version | Source | License and conditions |
| --- | --- | --- | --- |
| cJSON | 1.7.19~2 | [Registry](https://components.espressif.com/components/espressif/cjson) | MIT. Keep its copyright and license notice. |
| LVGL | 9.5.0 | [Registry](https://components.espressif.com/components/lvgl/lvgl) | MIT, with additional notices for optional bundled libraries and fonts. The collected notices are under `LICENSES/components/lvgl__lvgl/`. |
| esp_new_jpeg | 1.0.2 | [Registry](https://components.espressif.com/components/espressif/esp_new_jpeg) | Espressif MIT variant. Its grant says use on Espressif Systems products; this firmware targets ESP32-S3. Keep the notice. |
| esp_video | 2.0.1 | [Registry](https://components.espressif.com/components/espressif/esp_video) | Espressif MIT variant with the same Espressif-product condition; this firmware targets ESP32-S3. Keep the notice. |

Exact license texts collected from the locked component copies are under
`LICENSES/components/`. Some packages contain additional optional-code
licenses; those notices are preserved even when the K151 configuration does
not compile every optional file.

## Simulator dependencies

- [LVGL 9.5.0](https://github.com/lvgl/lvgl/releases/tag/v9.5.0), MIT.
- [SDL 2.32.10](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10), zlib license.

The simulator fetches these sources only when necessary. See
`firmware/esp32/simulator/THIRD_PARTY.md`.

## Services and excluded material

Muse SDK tokens and service access are governed by the
[Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms), separately from the
Apache-2.0 source license. MiniMax TTS use is governed by the user's own
MiniMax account and terms. Neither credential may be committed.

Jollybot's renderer, GIF, generated animation data and related Muse banner
images are excluded. The upstream Apache-2.0 license does not cover Jollybot,
and no separate permission sufficient for this publication was confirmed.
See `docs/ASSET_PREPARATION.md`.
