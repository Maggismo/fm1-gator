# Building GATOR

## What you need

- Python 3 with Pillow (`pip install -r requirements.txt`)
- JieLi's Linux toolchain (clang for pi32v2): `sh tools/get_toolchain.sh` puts it in `~/.jieli/toolchain`
- Docker on macOS or ARM Linux: the toolchain is linux/amd64 only, and each tool then runs in a container.
  On x86-64 Linux it runs natively.

The three JieLi AC79 SDK files every package holds (Apache-2.0) are in `tools/sdk/`; `AC79_SDK` can point at a full
SDK checkout instead. `tools/build.py` checks their SHA-256.

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
sh tools/get_toolchain.sh
PYTHON=.venv/bin/python ./build.sh                 # build/gator.fwsc       (48 kHz)
PYTHON=.venv/bin/python ./build.sh --app demo      # build/demo.fwsc        (the small demo synth)
```

Options: `--app NAME` (`firmware/apps/NAME`, default `gator`), `--fs 48|44` (the codec clock,
`firmware/sys/fw_rate.h`: 48 kHz by default; 44 builds `gator-44k.fwsc` at 44.1 kHz for comparison), `--name NAME` (the package's file name), `--release X.Y` (identity `FM-1_9XY`, version
`X.Y` on the splash). Environment: `FW_UART=0` (no TRS MIDI), `FW_UBOOT_COMBO=0` (no OCT−+OCT+ 5 s), `FELUCCA_OTA_DRYRUN=1`
(an update runs to the end but writes nothing), `JOBS=N` (parallel C++ compiles). C++ is rebuilt incrementally
(`build/cxx/APP/`).

Every build checks the image: the app slot and RAM sizes, 8 KB of pool headroom, no ROM calls, no soft-float
routines, no `.init_array`, and no register access outside `firmware/hal/`.

## Install a local build

```bash
pip install mido python-rtmidi
python3 tools/fm1_install.py --info               # identity of the connected FM-1
python3 tools/fm1_install.py build/gator.fwsc
```

Or from the browser: `python3 web/make_site.py` makes the installer site from the local builds in `build/site`;
`python3 -m http.server -d build/site 8000`, then http://localhost:8000 in Chrome or Edge (Web MIDI needs localhost
or HTTPS).

## Host simulator and tests

The app's code runs on the host against a stub HAL, built with ASan and UBSan (any finding fails the run): sound to
WAV, the screen to PNG. It says nothing about timing or CPU load: that is measured on the device only.

```bash
.venv/bin/python tools/sim.py                      # host/gator.sim -> build/host/out/*.png
.venv/bin/python tools/sim.py host/models.sim      # all 24 models in turn, models.wav
.venv/bin/python tools/sim.py --app demo
.venv/bin/python tools/sim.py --test               # storage test (A/B slots, an interrupted write)
node web/test_web.mjs build/gator.fwsc             # the installer's protocol against an emulated FM-1
```

A script is a list of lines: `frames N`, `key K down|up`, `btn PLAY down|up`, `knob K1 3`, `master 800`,
`midi 0x90 60 100`, `screen out.ppm`, `wav out.wav`, `logo`, `leds`, `bench MS`.

## Releasing

A release is a tag `vX.Y` (one digit each: the package identity is `FM-1_9XY`).

1. Add `docs/releases/X.Y.md` (the release notes) and commit.
2. `git tag v0.1 && git push origin v0.1`.
3. `.github/workflows/release.yml` builds the package (`tools/release.py`), runs the simulator and the installer
   test, and creates the GitHub release with `gator-X.Y.fwsc`, `SHA256SUMS` and the licences.
4. It then starts `.github/workflows/pages.yml`, which takes the package from the release, checks it against
   `SHA256SUMS`, makes the site (`web/make_site.py --release`) and deploys it to GitHub Pages.

The same locally: `tools/release.py X.Y` makes `build/release-X.Y/`, and
`python3 web/make_site.py --release build/release-X.Y` the site.

GitHub Pages must be set to deploy from GitHub Actions once: Settings → Pages → Source: GitHub Actions.
`.github/workflows/ci.yml` builds and tests every push and pull request.
