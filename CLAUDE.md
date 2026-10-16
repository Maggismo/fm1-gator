# GATOR

A macro-oscillator firmware for the M-VAVE FM-1 (JieLi AC79, pi32v2), on the Felucca platform cut down from
dspaudio/orbit (GPL-3.0-only). The DSP is Plaits (MIT, `third_party/eurorack`). Public repo: everything in English.

- Unity build: `firmware/sys/fw.c` includes every `.c`; include order matters. Apps live in `firmware/apps/NAME/`
  (`app.c` + optional `*.cc` + `third_party.txt` + optional `name.txt` / `logo.png` for the splash); `--app NAME`
  selects one (default `gator`; `demo` is the small example). The logo is drawn in canvas strips (LCD DMA reads RAM only).
- C is built without FPU (generic, as ORBIT); C++ with `-mcpu=r3 -mfprev1`. Cross the boundary with ints/pointers only.
  No static constructors (build fails on `.init_array`). `third_party/eurorack` changes go in its `PATCHES.md`.
  Do not use "Mutable Instruments" or "Plaits" in the product's name or on its screen (the author's guidelines);
  in texts say "inspired by Mutable Instruments Plaits", credit Emilie Gillet's DSP code, and add "not affiliated with or endorsed by".
- Plaits' `Voice::Render` must get exactly `kBlockSize` (12) frames: its envelopes assume it.
- The sample rate lives in `firmware/sys/fw_rate.h` only (C, C++ and the HAL clock bit); `--fs 48` (build and sim) selects
  the 48 kHz ALNK clock (both rates run on the author's unit). `FW_FS` is the rounded integer, `FW_FS_HZ` the exact float.
- `firmware/hal/`, `firmware/loader/`, `sys/usb.c`, `sys/ota.c`, `sys/recovery.c`, `sys/storage.c`, `sys/midi_uart.c`
  come from ORBIT and are hardware-verified there: change them only with a reason. The loader must stay byte-identical
  unless deliberately changed (it is what makes reflashing safe).
- Register access only in `hal/`; `tools/build.py` fails on MMIO literals, volatile casts or inline asm in `sys/`, `apps/`, `loader/`.
- `app_audio` runs in the ALNK0 ISR (256 frames, 5.8 ms); TIMER5 (input, USB) outranks it. No float in C, no malloc.
- Target build: `./build.sh` (Docker on macOS; the SDK files are vendored in `tools/sdk/`, Apache-2.0). Host check:
  `python tools/sim.py` (ASan/UBSan, fatal: a non-zero exit is a finding). Host results say nothing about timing.
- `sys/audio.c` acknowledges the ALNK half before rendering (ChoralRoot 0.18, heard on hardware); OTA sets
  `fw_audio_hold` so the app does not render; `fm1_cstart` raises the supplies to stock (SLOOP 2.7). Keep these.
- The envelope's times: `env_time_us` (app.c, integer table) and `env_seconds` (macro.cc) are the same curve
  (1 ms * 10^(4 v/1000)); change both. The matrix's LEVEL is linear gain (`level_cv` undoes Plaits' compression).
- Web installer: `web/` (fm1ota.js / fm1pkg.js are SLOOP 2.7's, vendored unchanged; the page and installer.js are
  ours, English only, dark). `web/make_site.py [--release build/release-X.Y]` -> `build/site`;
  `node web/test_web.mjs PKG` is the protocol test.
- Releases: tag `vX.Y` (one digit each, identity FM-1_9XY) -> `.github/workflows/release.yml` (`tools/release.py`) ->
  `pages.yml`. Release notes in `docs/releases/X.Y.md`. Licences: `LICENSE`, `LICENSING.md`, `LICENSES/` travel
  with every package (release and site).
- New files carry `SPDX-License-Identifier: GPL-3.0-only`; keep the existing copyright headers of platform files.
- The splash stays `FW_SPLASH_MS` (main.c) from drawing it; `app_init` runs meanwhile and must not draw.
- Never claim hardware testing that did not happen.
