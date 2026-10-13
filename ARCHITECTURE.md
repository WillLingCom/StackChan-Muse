# ARCHITECTURE — StackChan K151 × Muse

Base: Muse ESP32 Device SDK (facebookincubator/muse-gadget-sdk @74a5e2d), branch `k151` in `firmware/`.
New board `m5stack_stackchan` (Kconfig `MUSE_BOARD_M5STACK_STACKCHAN`), overlay `devices/sdkconfig.muse-m5stack-stackchan`.
One `app_main`, one LVGL, one I2S owner (the Muse voice task), one I2C bus (CoreS3 internal, port 1) shared by all devices.

## Layers

```
Muse (cloud agent) ──Noise/Link──► noise_control.cpp ──link.invoke──► app.c on_ws_command
        ▲  link.register (commands_v2)        │                              │ unknown command
        │                                     ▼                              ▼
        │                         muse_board->tools_register      muse_glue_board_command
        │                                                            │  (async: request_id + session generation)
        │                                                            ▼
        │                                                stackchan_tools.c  robot.gesture / robot.stop /
        │                                                                  robot.status / camera.capture
        │
  chat (voice notes, replies) ◄──── muse_chat_session.cpp ◄── muse_voice.c (record / VAD / hands-free)
                                     └─ start_tts → muse_tts.c (MiniMax T2A) → minimp3 → muse_audio (I2S)
```

## Modules (new or changed for the K151)

| Module | File(s) | Role |
|---|---|---|
| Board | `components/muse/boards/board_m5stack_stackchan.c` | CoreS3 via Espressif BSP (LCD, touch, AW88298, ES7210, AXP2101); wires the body modules, console commands, tools, settings switch |
| Head touch | `stackchan_head.c`, `stackchan_head_gesture.c` | Si12T (0x68) polled 50 ms; debounce, landing window, stroke/lift-off rules (host-tested); drift recalibration after 20 s |
| Body / servos | `stackchan_body.c`, `stackchan_servo_regs.h` | PY32 reads; SCSCL servos on UART1; write whitelist (regs 40, 42–47 only, host-tested); nudge; gesture executor (50 Hz, two axes, load/lag aborts, safe stop) |
| Gestures | `stackchan_gesture.c` | Planner: nod, shake, sway, look_left/right, home (+ local turn_left/right); soft limits, speed limits, 5 s cap (host-tested) |
| Tools | `stackchan_tools.c`, `stackchan_tools_parse.c` | Muse tools; parameter validation and duplicate-request refusal (host-tested); one final result per request |
| Camera | `stackchan_camera.c` | GC0308 over esp_video DVP; YUYV 320×240; 8 warm-up frames; software JPEG (esp_new_jpeg); stream only while capturing; 12 KB DMA buffer |
| Mic tap / recorder | `muse_audio.c` (`muse_audio_set_tap`), `stackchan_mic.c` | Raw stereo before the mono mix; short recordings on request |
| Direction (experimental) | `stackchan_doa.c` | Band-passed cross-correlation, speech-gated, per-turn median, conservative decision (host-tested) |
| Reactions | `muse_state.c` (`muse_state_react`), `muse_ui.c` | Surprised/shy overlays independent of the avatar renderer; idle only |
| Touch input | `muse_ui.c`, `muse_input.c` | Tap / double tap / long-press talk; hands-free tap; screen press stops motion |
| Hands-free | `muse_vad.c`, `muse_voice.c` | VAD endpointing (floor tracked while idle, steady sound absorbed, quiet relative to the turn's peak); conversation window; nothing streamed before 400 ms of speech; turn events |
| TTS | `muse_tts.c` (user's MiniMax adapter) | Reply text → MP3 → speaker; cancels with the turn |
| Dance (F8) | `stackchan_dance.c` (pure), `stackchan_dancer.c` | Original tunes (synth), choreography as a function of the playhead (host-checked); music as a voice-task job after the reply; head via `stackchan_body_follow` (speed-capped); lights + screen tint |
| Base LEDs / power | `stackchan_rgb.c`, `stackchan_py32_regs.h`, board init | PY32 write whitelist (pin 0 and pin 13 config, LED regs); AW9523 P0_1/P1_7 on at boot (5 V to the base) |
| Infrared (F9) | `stackchan_ir_codec.c` (pure), `stackchan_ir.c` | RMT TX GPIO5 / RX GPIO10; NEC + raw (first frame, repeats, mark-excess correction); NVS "k151_ir" (10 codes, room kept for Muse) |
| Sounds | `stackchan_sfx.c` (pure) | Touch and motion reaction sounds, played as voice-task jobs, rate-limited |
| Motion play | `stackchan_motion.c` (pure), `stackchan_imu.c` | BMI270 50 Hz → shake (energy + reversals), tilt, slip, lift, flip → avatar (`muse_pose_t` dizzy/tilt/scared) and sounds; idle only, ignored while servos move |
| Voice jobs | `muse_voice.c` (`muse_voice_request_job`) | A board's audio (dance, sounds) runs on the voice task once a turn is over |

## Arbitration

Priority: local stop / fault > user's explicit task > touch feedback > automatic behaviour.

| Resource | Owner / rule |
|---|---|
| I2S (mic + speaker) | Muse voice task only; mic raw stereo shared via one tap (DOA or recorder) |
| Servos (UART1) | One move at a time (`s_moving`); bus mutex; head touch or screen press stops; refused under a hand, while recording (tools), or while moving |
| Camera | One capture at a time; stream only during capture |
| Screen (LVGL) | Upstream lock; reactions only while idle; captions from the voice task |
| I2C bus | Shared handle, per-device speeds (Si12T and PY32 at 100 kHz) |

## Safety-relevant invariants

- Servo writes limited to torque and goal registers; EEPROM (ID, baud, angle limits) and lock never written.
- Goal set to the current position before torque on; every move ends with torque off at a resting pose; stop = hold, then lower pitch slowly.
- No OTA, no NVS encryption, no Secure Boot / flash encryption in this build (script refuses otherwise); no eFuse writes.
- Hands-free audio is not streamed before 400 ms of detected speech; nothing is listened for outside a conversation the user started.
