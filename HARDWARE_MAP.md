# HARDWARE_MAP — M5Stack StackChan K151

Sources (pinned, see SOURCE_PROVENANCE.txt): **SC** = m5stack/StackChan `firmware/` @1b57655 (factory firmware),
**BSP** = m5stack/StackChan-BSP @8d4d6fc, **CM** = stack-chan/stack-chan @2f6b5a6,
**ES** = espressif/m5stack_core_s3 BSP 4.1.0 (managed component used by Muse).
"Device" column: what was verified on the attached unit (MAC <YOUR_DEVICE_MAC>). "—" = not yet verified on hardware.
No I2C scans or register probes were run; facts below come from source, not guessing from scans.

## Chip / memory
| Item | Value | Source | Device |
|---|---|---|---|
| SoC | ESP32-S3 (QFN56) rev v0.2, dual core | esptool | ✔ esptool chip-id |
| Flash | 16 MB, mfr 0x46 dev 0x4018, DIO in Muse build | esptool flash-id | ✔ |
| PSRAM | 8 MB external quad, 80 MHz (eFuse PSRAM_CAP=0: not in-package) | Muse boot log `esp_psram: Found 8MB` | ✔ S1 boot |
| USB | native USB-Serial/JTAG 303a:1001, serial = MAC | ioreg | ✔ |
| Security | Secure Boot off, flash encryption off, no keys, download mode on | espefuse summary | ✔ |

## I2C — one internal bus: I2C port 1, SDA GPIO12, SCL GPIO11 (SC stackchan.cc:386-402, config.h:18-19; ES m5stack_core_s3.h:74-75, Kconfig BSP_I2C_NUM=1)
| Device | Addr | Speed used | Owner in Muse build | Source |
|---|---|---|---|---|
| AXP2101 PMU | 0x34 | — | ES BSP + muse_pmu (key/battery) | SC stackchan.cc:430; ES bsp_feature_en.c:15 |
| AW9523B IO expander | 0x58 (AW9523_ADDRESS_00) | — | ES BSP | SC stackchan.cc:436; ES m5stack_core_s3.h:706 |
| FT6336U touch | 0x38 | — | ES BSP (esp_lcd_touch ft5x06) | SC stackchan.cc:454; ES bsp_display.c:67 |
| AW88298 amp | AW88298_CODEC_DEFAULT_ADDR | — | ES BSP (esp_codec_dev) | ES bsp_audio.c:114 |
| ES7210 mic ADC | ES7210_CODEC_DEFAULT_ADDR | — | ES BSP | ES bsp_audio.c:153 |
| GC0308 camera SCCB | (esp_video driver) | 100 kHz | not yet used | SC stackchan.cc:567-571 |
| BMI270 IMU | 0x69 | 400 kHz | not used | SC hal_imu.cpp:48 |
| BM8563 RTC | 0x51 | 400 kHz | not used | SC PCF8563_Class.hpp:29 |
| **Si12T head touch (body)** | **0x68** | 100 kHz | not yet used (S2) | SC Si12T.h:16, Si12T.cpp:72 |
| **PY32 base controller (body)** | **0x6F** | 100 kHz | **deliberately untouched in S1** | SC PY32IOExpander_Class.hpp:16, hal_io_expander.cpp:20 |
| INA226 battery V/I | 0x41 | — | not used (BSP only) | BSP M5StackChan.cpp:320-325 |
| LTR-553, BMM150, NFC | not found in SC source | | not used | |

## Display / touch
| Item | Value | Source |
|---|---|---|
| Bus | SPI3, MOSI 37, SCLK 36, CS 3, DC 35, 40 MHz, mode 2 (SC); CM lists different pins (DC15/CS5) — SC/ES are authoritative for this firmware | SC stackchan.cc:477-505 |
| Panel | ILI9342C (or E variant, detected via FT6336 fw id) 320×240, BGR, inverted | SC stackchan.cc:508-534; ES priv_include/ili9342e_init_cmds.h |
| LCD reset | AW9523 P1.1 | SC stackchan.cc:213-220 |
| Backlight | AXP2101 DLDO1 (reg 0x99 voltage, 0x90 enable) | SC stackchan.cc:119-141; ES bsp_feature_en.c (REG_LCD_BL 0x99) |
| Touch | FT6336U polled; INT/RST not used | SC stackchan.cc:275-298 |

## Audio
| Item | Value | Source |
|---|---|---|
| I2S | I2S0 master full duplex; MCLK 0, BCLK 34, WS 33, DOUT 13 (spk), DIN 14 (mic) | SC config.h:12-16 |
| Factory rate | 24 kHz; ES7210 TDM, ch0 = mic, ch1 = echo reference | SC config.h:9; cores3_audio_codec.cc:142-206 |
| Muse rate/format | MUSE_AUDIO_RATE, 16-bit stereo std I2S via ES BSP; **mono mix at muse_audio.c:258** (DOA must tap before it) | Muse board_m5stack_stackchan.c audio_init; muse_audio.c:254-259 |
| Rails | AXP2101 ALDO1 speaker, ALDO2 mic, ALDO3 camera, ALDO4 SD | ES bsp_feature_en.c:16-21 |
| AW88298 reset | AW9523 P0.2 | SC stackchan.cc:195-211 |
| Mics | two MEMS ports below the screen, left/right ≈3 cm apart (user). Muse stereo slot L = viewer-left hole (robot's right), slot R = viewer-right hole (robot's left): near-field TDOA −1.37 / +1.26 samples ≈ the ±1.40 limit at 16 kHz. Idle hum 27–73 Hz dominates the raw floor (−53 dBFS); speech band floor −64 dBFS | S5 recordings |

## Camera GC0308 (S4)
D0–D7 = 39,40,41,42,15,16,48,47; VSYNC 46, HREF 38, PCLK 45; XCLK not driven (20 MHz on-board crystal per comment); PWDN/RESET n.c.
Factory mode `GC0308_DVP_YUV422_320X240_20FPS`, format YUV422/RGB565 (no hardware JPEG). Rail ALDO3.
Source: SC config.h:45-62, sdkconfig.defaults:53-54, stackchan_camera.cc:217-239; ES bsp_feature_en.c.

## Servos (S3) — NOT touched until S3
| Item | Value | Source |
|---|---|---|
| Type | Feetech SCSCL half-duplex serial bus servos with position/load/current feedback | SC hal_servo.cpp:7,16 |
| Bus | UART1, TX GPIO6, RX GPIO7, 1 000 000 baud 8N1 | SC hal_servo.cpp:335; SCSerial.cpp:33-67 |
| IDs | yaw (horizontal) = 1, pitch (vertical) = 2 | SC hal_servo.cpp:338,347 |
| Units | raw 0–1000, 0.3125°/step; software angle in 0.1°: raw = zero + angle·16/5/10 | SC hal_servo.cpp:76-77,204-207 |
| Zero | defaults yaw 460, pitch 620; **this unit: yaw 450, pitch 610** (factory NVS ns `servo`, `zero_pos_1/2`, read offline from the backup; factory NVS @0x9000 is still on flash, unused by Muse) | SC hal_servo.cpp:45-72,339-352 |
| Factory SW limits | yaw ±128.0°, pitch 3.0–87.0° (BSP: 0–90°, CM: 5–85°) — our limits will be tighter | SC hal_servo.cpp:340,349 |
| Power | PY32 pin 0 "VM EN" (output, driven high by factory init) | SC hal_io_expander.cpp:41-45,61-67 |
| Registers | goal pos 42, torque 40, pos 56, moving 66, load 60, current 69 | SC SCSCL.h:16-47 |
| **Forbidden** | angle-limit EEPROM regs 9–12 (factory `rotate()`/SwitchMode writes them), ID/baud/Reset/Recal | SC SCSCL.cpp:183-237 |
| Factory boot behaviour | snaps to current pos, torque off; no auto move at HAL init | SC servo.cpp:28-36 |

### Measured on this unit (S3 phase 1, read-only, firmware 3094360)
| Item | Yaw ID 1 | Pitch ID 2 |
|---|---|---|
| Answers SCS READ @1 Mbaud, checksum OK | ✔ | ✔ |
| Firmware version (reg 3-4) | 1284 | 1284 |
| EEPROM angle limits (reg 9-12) | 20–1003 (unmodified) | 20–1003 |
| Torque at boot (reg 40) | off | off |
| Resting position | raw 454 = +1.2° | raw 595 = −4.7° (below the factory 3° minimum: limp, resting low) |
| Voltage / temperature | 5.1 V / 27 °C | 5.1 V / 27 °C |
PY32 version 0x41. VM EN (pin 0) is still an output latched high from the factory firmware: the PY32 keeps its state
across ESP32 resets/reflashes, so the servos stay powered (5.1 V) while torque is off. Its input register reads 0 for
this output pin, so "level" isn't a power indicator; use the latch and the servos' voltage.

Directions and resolution (S3 phase 2a nudges): **yaw raw increasing = head turns to the viewer's left (robot's right)**;
pitch raw increasing = up (inferred: rests at −4.7° below the factory 3–87° range). Dead band reg 26/27 = 1, yet the servos
start only at ≈8–9 ticks of error and settle ≈5–6 ticks short; present current reads 0 (unusable), load ≈150–165 holding.

## Head touch Si12T (S2)
I2C 0x68, polled 50 ms; init regs 0x0A–0x0F=0, CTRL2 0x09=0x0F→0x07, CTRL1 0x08=0x22, sensitivity 0x33 to 0x02–0x06;
read OUTPUT1 0x10: three zones × 2 bits (0–3). Zone order verified on this unit (user test): **zone 2 = front of the head, zone 0 = back**.
Source: SC Si12T.cpp:136-310, hal_head_touch.cpp:19-159; BSP touch_sensor.cpp:37,83-96.

## RGB / IR / power to the base (F8/F9, implemented)
12 RGB LEDs driven by the PY32 (pin 13; count reg 0x24, RGB565 RAM from 0x30; LEDs 0–5 = viewer's left, 6–11 = right; verified) — SC hal_io_expander.cpp:47-55, PY32IOExpander_Class.cpp:325-361.
IR TX GPIO5 (RMT, 38 kHz, 1/3 duty), RX GPIO10 (RMT, demodulated, active low) — BSP examples/IR/Send/Send.ino:12, Receive.ino:16. **The IR window (LED + receiver) is on the top of the head** (user, 2026-10-08). Reach ~1 m on axis; the receiver stretches marks by ~90 µs (measured by echo).
**5 V to the base's LEDs and the IR emitter needs AW9523 (0x58) P0_1 and P1_7 high** (factory stackchan.cc Aw9523: P0 0b00000111, P1 0b10001111; user-confirmed). The Espressif AW9523 driver resets all pins low at init; the board sets both again. PY32 pin 0 (base power) set output, pull-up, push-pull, high as the factory BSP does (its input register still reads 0 while the rail works).
IMU: BMI270 at 0x69 in the CoreS3 (head): at rest +y ≈ +1 g (screen vertical), z = screen normal. Moves with the head: ignored while servos move.

## Remaining hardware questions
- Long-duration and battery-powered behavior remain unverified.
- Head-touch zone order and microphone mapping were verified later; see the S2/S5 notes above.

The initial peripheral table is an S1 bring-up snapshot, not a current feature list.
Camera, head touch, IMU, base LEDs, servos and IR were integrated in later stages.
