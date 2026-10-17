# Building LOOM-8

LOOM-8 is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (in `base/`, a git submodule, with
[felucca-bsp](https://github.com/hugelton/felucca-bsp) inside it as `base/bsp`). Clone with the submodules:

```
git clone --recursive https://github.com/charlesvestal/fm1-loom
git submodule update --init --recursive      # (an existing clone)
```

The build (fm1-base's `tools/build.py`, run by `./build.sh`, driven by `app/fm1.toml`) makes three files in
`app/build/`:

| File | What |
| --- | --- |
| `loom.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `loom.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow built with libraqm (the fonts need it; Homebrew's: `PYTHON=/opt/homebrew/bin/python3.14`)
- Docker Desktop, or podman. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source tree in a folder the
  container can share, e.g. under `/Users`. With podman, put a `docker` script that runs `exec podman "$@"` first
  on your PATH. `FM1B_JOBS` (default 4) limits parallel compiles: a podman machine drops connections when many
  containers start at once.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  base/tools/get_toolchain.sh       # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Emscripten and Node.js (optional: the browser emulator and its test).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh                    # the firmware: app/build/loom.fwsc
./build.sh --host             # the host simulator: app/build/host/loom_host
./build.sh --web              # the browser emulator: app/build/emu/ (LOOM-8's own page, app/web/emu/)
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).
`./build.sh --release 0.1` makes a release build: the package is `app/build/loom-0.1.fwsc`, its identity
FM-1_10XXYYZZ (a dev build's is FM-1_1000).

The build generates `app/build/gen/` (fm1-base's fonts for its own screens, LOOM-8's fonts, the wavetables, the
factory samples, the screen reader's words link, this build's number). It runs LOOM-8's size pass
(`tools/size_fns.py`: the code that never runs per sample built with LLVM's `minsize`), and checks: no
soft-double routine (a `double` crept in), no register access outside felucca-bsp (LOOM-8's own second core,
`app/cpu1/`, aside), nothing in RAM code calling the flash (the second core's loop aside), no 64-bit multiply
inlined beside a load, and the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the units' own tests (the synth, the drumline, the FX, the sequencer:
`tests/host/*_test.sh`); the DSP's fingerprints against a reference (`tests/host/dsp_hash.sh save` before a change
that must not move a sample, then the tests compare); the user sample bank's tool; fm1-base's update-path tests
against `app/build/loom.fwsc`; the browser build; and the whole app in fm1-base's simulator
(`tests/scenarios/*.xy`, backup and restore among them), with its screenshots and audio in `app/build/scenarios/`
and the layout audit. Silent: the audio goes to WAV files. fm1-base's own tests: `base/tests/run_tests.sh`.

`app/build/host/loom_host SCRIPT OUTDIR` runs one script: fm1-base's commands are at the top of
`base/host/plat_host.c`, LOOM-8's own at the top of `app/host.c`. `tools/gen_screens.sh` makes the manual's
screenshots (`tools/manual_screens.xy`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/loom.fwsc
python3 base/tools/fm1_install.py --info     # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for the update mode. Two failed starts
in a row bring up a safe mode with USB on; the web installer can put M-VAVE's firmware back.
`base/tools/fm1_remote.py` drives an FM-1 over USB and reads its screen, crash record and log.

Backup and restore of the projects and the user presets: `python3 base/tools/fm1_backup.py backup FILE --magic LM8B`
and `restore FILE` (or the site's Backup page). The user sample bank is not in a backup: `tools/loom_samples.py`
loads it from the WAV files.

## Publishing a release

1. `./build.sh --release X.Y` (the identity, FM-1_10XXYYZZ, is what the installer checks).
2. `tools/publish_pages.sh X.Y`: builds the site (landing page, browser emulator, fm1-base's web installer and
   backup page, the words files, the firmware) with `tools/make_pages.py` and pushes it to the `gh-pages` branch,
   which GitHub Pages serves at <https://charlesvestal.github.io/fm1-loom/>.
3. `gh release create vX.Y app/build/loom-X.Y.fwsc` for the command-line download, with release notes.
