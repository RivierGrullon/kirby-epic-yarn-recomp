# macOS Intel and Apple Silicon development

macOS support is experimental. The build entry point supports **native x86_64
on Intel** and **native arm64 on Apple Silicon**, with Metal graphics. Neither
architecture is declared gameplay-validated by the presence of a passing CI job.
Releases can package Kirby Setup for Intel and Apple Silicon without game data.

## Prerequisites

- A Mac running macOS 13 or newer, with a Metal-capable GPU.
- Xcode Command Line Tools (`xcode-select --install`), Git, Python 3.11+,
  CMake 3.25+, Ninja, pkg-config and the .NET 8 SDK.
- A Qt 6.4+ desktop SDK for the native architecture. Its own minimum macOS
  version can raise the deployment requirement. Use a matching SDK/toolchain
  when testing older versions of macOS.
- Your own USA **RK5E01 Rev 0** disc or complete extracted DATA directory.
  `dolphin-tool` on PATH is required for a disc image; 7-Zip is only needed for 7z.
- Several GB of available RAM and disk space. Start with two compiler jobs.

Homebrew can supply CMake, Ninja, pkg-config, Python and Qt. Install the .NET 8
SDK for your architecture from Microsoft. Run Terminal and Python natively;
Rosetta sessions and cross-compilation are rejected to prevent mixed libraries.
A Mac Intel build is not an ARM build. Run the same command on each type of Mac.

## Kirby Setup

Open **Kirby Setup.app** from the release ZIP. Select your disc image or
extracted DATA folder and an installation destination, then choose **Install**.
Setup extracts and verifies the data, compiles the game and launcher, checks game
startup, and installs **Play Kirby.command** in the chosen destination. Open the
launcher from Setup or use that command later.

The installer uses the native tools listed above. It finds Qt with Homebrew
or the `KIRBY_QT_PREFIX` environment variable, and finds Dolphin development builds
that include the command-line extractor
in Applications or `dolphin-tool` on PATH. The Setup bundle is ad-hoc signed.
It contains the Setup UI and source snapshot, and builds the launcher locally.

## Developer build

From the repository checkout (replace the example DATA path):

```bash
python3 tools/macos/build_macos.py "/actual/path/to/DATA" \
  --qt-prefix "$(brew --prefix qt)" --package
```

Use `--check` to check the tools without downloading, translating or compiling.
Use `--arch x86_64` or `--arch arm64` to explicitly require the matching host.
For a non-Homebrew Qt installation, pass its desktop kit directory instead.
WBFS/ISO/RVZ inputs are extracted by the existing verified source importer.
A `main.dol` is accepted for compilation, but playing requires all DATA files.

The entry point holds the setup lock, verifies the DOL, bootstraps the pinned
dependencies, translates on macOS (including Mach-O data assembly), then builds
the runtime and Qt launcher. It checks all three executable architectures and
runs bootstrap and, when DATA is available, initialization probes in a private
working directory. These probes do not validate Metal gameplay.

| CPU | Build directory | Local application |
| --- | --- | --- |
| Intel | `build/macos-x86_64/` | `local/releases/Kirby-macOS-x86_64.app` |
| Apple Silicon | `build/macos-arm64/` | `local/releases/Kirby-macOS-arm64.app` |

Use `--deployment-target 14.0` (for example) when your dependencies require a
higher minimum OS. On Intel, Setup selects x86-64-v3 when native Clang detects
all required extensions. Other Intel Macs use x86-64-v2. A game built with
v3 requires a v3-capable CPU if you move it to another Mac. Apple Silicon
builds use the M1 instruction baseline.
CMake caches and runtime snapshots are separate for the two architectures.
Do not run different builds concurrently in one checkout.

Without `--package`, launch `build/macos-x86_64/kirby-launcher` or
`build/macos-arm64/kirby-launcher`. Select the complete DATA directory on the
Install page. This developer launcher stores settings/saves in the checkout.

## Local application bundle

To package an existing build:

```bash
python3 tools/macos/package_macos.py --qt-prefix "$(brew --prefix qt)"
```

The packager uses `macdeployqt` to bundle libraries and plugins, applies an
ad-hoc signature, verifies that signature, and runs the relocated game metadata
query and Qt self-test with development library paths removed. It refuses to
replace an existing app; choose a new `--output /path/to/Kirby-test.app` for an
update. Move the resulting app out of the source directory and test it again.

The app keeps writable state under
`~/Library/Application Support/PortingLab/KirbyEpicYarn/`. Saves are in
`local/user/nand/title/00010000/524b3545/data/` below that directory. Intel and
Apple Silicon apps use the same data location. Existing checkout saves are not
automatically moved; use the launcher's save export/import tools.

**This compiled application is for local use.** It contains translated game
code generated from your disc. Do not attach it to public releases. A public
Mac Setup ZIP contains only the installer and project sources. Automated
installation of the macOS toolchain is a follow-up. Developer ID signing and
notarization are also not supplied by this patch. There is no universal binary
or claim of cross-architecture gameplay compatibility yet.

## Validation before a release

The macOS workflow uses separate Intel and ARM runners. It compiles/runs the
pinned runtime's host tests, Kirby's NAND/disc and process tests, the native
launcher, Qt frontend and production renderer, without any game assets. Renderer
CPU contracts check successful readback notification, published bytes, stale jobs
and destroyed streams. It uploads no game build. These checks do not need a GPU.

The manual workflow's **run_metal_checks** option additionally runs the synthetic
GPU suite: native and scaled/MSAA copies, CPU-visible memory and FXAA pixels. It
requires an actual Metal device; a hosted runner without one cannot validate it.

On **each real Mac**, also verify:

1. A clean build from the supported disc, then first launch from Finder.
2. Title screen, save selection, Quilty Square, Fountain Gardens and a completed level.
3. Audio playback, fullscreen, resolution changes, FXAA/MSAA and Metal effects.
4. Keyboard and physical gamepad input, two-player input and motion controls.
5. Save creation, close/reopen, save import/export and backup restoration.
6. The relocated app with no Homebrew or Qt paths available; a second launch
   after replacing the app must retain the same saves.

Record model, CPU, GPU, macOS, toolchain versions, scene and logs. The build's
`--build-info` command identifies the exact source and dependencies tested.

## Intel performance status

A MacBook Pro 2020 with an Intel i5, 16 GB RAM and macOS Sequoia has been reported
to launch the game with substantial frame-rate drops. This is a startup report,
not a minimum specification or a stable-gameplay result. The year and CPU family
alone do not identify the exact Intel GPU. Apple Silicon gameplay remains to be
validated separately.

The port retains Metal, native Intel v3/v2 selection, the M1 ARM baseline and the
existing memory-coherence checks. It does not lower the original game's clock or
discard frames to hide slowdown. The RAM requirement for compilation is separate
from CPU/GPU performance while playing.

Synchronous GPU copies previously slept for a fixed 1 ms between completion
polls. Each copy job can now signal the waiting thread after its immutable packet
is published. A completed callback wakes the caller early; Dawn polling stays on
the render worker, with the same 5-second timeout and final worker fence. Copies
still verify their sequence, payload and CPU-visible bytes. Synthetic completion
and race tests establish the wakeup contract, not a measured gameplay FPS gain.

Before accepting Intel performance, compare the old and new build on the same
Mac, scene, camera, isolated save and settings, with the window visible. Start at
1x internal resolution, MSAA/FXAA off and experimental GPU copies on; keep those
settings identical between builds. Warm the shaders, use AC power and keep Low
Power Mode and other host conditions identical. Repeat a moving level segment,
not only the title screen. Capture with the existing diagnostic arguments:

```bash
/absolute/path/to/WiiCompiled --video --game-data /absolute/path/to/DATA \
  --config /absolute/path/to/test-settings.toml --volume 0 \
  --frame-log /absolute/path/to/new-frames.csv
python3 tools/frame_stats.py /absolute/path/to/new-frames.csv --start 120 --end 180
```

Run from a separate writable test directory to keep diagnostic saves isolated;
use absolute paths for the executable, config and DATA. The time window above is
an example: select the same actual gameplay segment in each trace. Report the
submission rate, p95/p99 intervals, readback count, worker waits and exact GPU.
Those timings separate submission stalls from guest work; they do not measure
physical display scanout. Audio and normal game speed also need a real check.

The renderer checks can be built separately from a translated game. Point
`KIRBY_AURORA_DIR` at the pinned **build snapshot with all Kirby Aurora patches
applied**, rather than an unmodified upstream checkout:

```bash
cmake -S tests/renderer -B build/renderer-checks -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DKIRBY_AURORA_DIR=/absolute/path/to/patched/aurora
cmake --build build/renderer-checks --parallel 2
ctest --test-dir build/renderer-checks --output-on-failure
build/renderer-checks/kirby_gx_renderer_test --gpu-xfb-hidden
build/renderer-checks/kirby_gx_renderer_test --gpu-quality 2 4
build/renderer-checks/kirby_gx_renderer_test --gpu-fxaa
```
