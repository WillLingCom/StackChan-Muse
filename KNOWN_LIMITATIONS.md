# KNOWN_LIMITATIONS — StackChan K151 × Muse

Status at firmware `611f5b5` (2026-10-09, tag `k151-v1.1`). Evidence in docs/VALIDATION.md.

## Muse integration
- Muse calls the robot tools only after it has been taught the skill (`muse_skill/SKILL.md`, pasted into the Muse app). Which tool it picks is Muse's decision; voice "摇摇头" was once mapped to `nod` (text mapped correctly; likely speech recognition).
- Photo Q&A: the Muse platform can't view a JPEG inside a tool result (camera.capture's result is read as text). Photos therefore reach Muse as an image attachment in a device-started chat turn right after camera.capture; this works end to end but takes two Muse turns (≈13–19 s from the tool call to the answer). The skill note must describe this flow. If the follow-up turn fails, the screen says "看图失败，请再问一次" (no tap-to-retry).
- Replies are spoken through MiniMax (the user's account, billed per reply). The key is compiled into the image: development only; a product needs a proxy.
- No Muse wake word ("Muse", F5) yet; hands-free conversations start with a press, a tap or a head touch.

## Voice
- Half duplex: no voice barge-in while Muse speaks (interrupt by PWR, screen tap or head touch). No AEC.
- Hands-free endpointing is energy-based: steady noise just above the threshold can keep a turn open up to the 15 s cap; a very soft voice may not start a turn.
- Each hands-free turn starts streaming ~0.4 s after speech begins (privacy hold-back).

## Motion
- Servos settle 4–8 ticks (≈1.3–2.5°) short of a goal; small moves (<3°) don't complete. Present-current register reads 0: only load is used for stall detection.
- Stroke-then-nod is disabled (a report of a nod not stopping under a hand; unresolved).
- Soft limits are tighter than the factory: yaw ±25°, pitch rest…rest+30°.
- If a hand stays on the head > 20 s, the touch chip recalibrates and may stop seeing that hand (screen tap still stops motion).

## Head touch
- Weak level-1 readings appear during motion and after long uptime (reference drift); motion stops on any debounced touch (false stops accepted). Quick strokes moving through only two positions are not counted.

## Camera
- 320×240 stills only; one capture at a time; ~0.5 s. Each capture restarts the DVP stream (a harmless "GPIO isr service already installed" error is logged).

## Direction (F6, experimental)
- Mics are 3 cm apart; at ~1 m the inter-mic delay is compressed to about ±0.2 samples (limit ±1.4). "Turn to voice (beta)" turns only with a clear margin, mainly close up. Labelled accuracy run not done. Not an omnidirectional locator; no front/back.

## System
- One unexplained reboot during a photo attempt (no log); the camera's low-RAM window was the likely cause and has been reduced (lowest internal 4 K → 22 K), not proven.
- Not measured: task stack high-water marks, multi-hour runs, battery-powered behaviour.
- OTA disabled; release signing/update not designed.

## Dance (F8)
- Two built-in original tunes only (no other music, no SD card). Motion lead (80 ms) is a fixed estimate, not measured with video.
- A level-1-only head touch must last 300 ms to stop a dance (brief blips appear with a person close by); firm touches and screen taps stop at once.

## Infrared (F9)
- The IR window is on the top of the head: aim the remote at it to learn, and point the head's top at the device to send. Reach ~1 m on axis, narrow beam; more pad drive / duty brought no extra reach (reverted).
- Raw codes only keep a capture's first frame (repeated up to 3x on send); remotes with frames split by gaps ≥ 8 ms (some air conditioners) are cut. The carrier is assumed (38 kHz). One-way: no confirmation the device acted.
- Needs AW9523 P0_1/P1_7 on (done at boot). Once the emitter stopped after the robot was shaken and handled (a head-top connection, fixed by the user); `ir.selftest` / `ir.pad` tell a dark emitter from a firmware fault.
- A Xiaomi TV in deep standby may only wake by Bluetooth.

## Motion play and touch sounds
- Shake/tilt/lift/flip only when idle and never while the servos move. The tilt sign was checked once by the user. Thresholds are from one person's shaking (imu.trace).

## Not implemented
- Wake word (F5), voice barge-in with AEC, proximity, NFC, microSD, battery gauge (INA226).
