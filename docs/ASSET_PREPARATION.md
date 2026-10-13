# Original visual experience: BLOCKED

Reviewed 2026-10-13. The original development project is untouched. The publication keeps the original `muse_pixel.h` API, `muse_ui.c`, state/expression logic, animation consumers, board overlays and fonts. No replacement artwork is installed.

## Official evidence

- The [Muse SDK license section](https://github.com/facebookincubator/muse-gadget-sdk#license) explicitly excludes Jollybot from Apache-2.0.
- The [official avatar recipe](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/esp32/tools/muse/AVATAR_RECIPE.md) documents `tools/muse/avatar.py`: it asks a paired Muse device to generate a personal character, saves an ignored `components/muse/avatar/muse_pixel.c`, validates it, builds and normally flashes. `--no-flash` avoids its flash step. The recipe calls for one's own name and look; it is not a grant to redistribute Jollybot.
- The [official animation generator](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/esp32/tools/gen_happy_anim.py) converts the existing Jollybot GIF into C data. Its Apache-licensed tool code is retained. Conversion does not provide a separate license to the input artwork or output character.
- [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms) govern tokens and Muse access separately from source-code licensing. A free/non-commercial project does not by itself resolve excluded artwork permissions.

No explicit permission or licensed asset package sufficient for this project's automatic download/use and redistribution requirements was located in those official sources. This is an unresolved permission question, not a claim that all private uses are prohibited. No unconfirmed artwork downloader is enabled.

## Why downloading the SDK is not enough

The fixed official baseline is `74a5e2d7fc895f109f83a9a1dbed705dbcd8b1ff`. The prototype's GIF and generated legacy animation files match it, but the prototype's `avatar/muse_pixel.c` differs from that baseline. Fetching the official renderer alone therefore cannot establish identical motion/expression behavior. The modified renderer remains excluded too; it is not published as a patch or encoded asset.

[assets-status.json](assets-status.json) records expected hashes, paths and this distinction without embedding artwork. Copyrighted assets remain excluded from Git even if someone later obtains permission for local use. This status file is a technical inventory, not a license grant.

The official personalized-avatar flow also requires a working paired board, service access and nondeterministic generation. It cannot provide a guaranteed, clean-machine, one-command reproduction of this prototype.

## Preserved interfaces

- `muse_pixel_accent`, `muse_pixel_render`, `muse_pixel_set_size`, `muse_pixel_scale` and `muse_pose_t` are unchanged.
- Original `happy_anim_*` symbols and CMake references are unchanged.
- The source UI, motion reactions, font data and board runtime files are unchanged by publication preparation.
- Builds stop explicitly when the missing original assets are required. Host parser tests use their own non-rendering text fixture instead of reading an excluded artwork file.

## Conditional one-command design

After permission is established, a dependency preparer could fetch only explicitly authorized files from a pinned official revision, verify SHA-256, keep them in ignored local paths and validate them against the prototype manifest. Any local renderer changes would need a cleared distribution path too. ESP-IDF Component Manager would then fetch the locked ordinary dependencies before `scripts/k151.sh build-public` builds the firmware.

This design is **not enabled or validated**. It must not be implemented by merely adding an “I agree” flag or silently substituting a new character. The remaining decision is whether the rights holder can authorize local download/use, modified-renderer distribution, and any desired binary redistribution. Written permission or an updated explicit official license would make that review concrete.
