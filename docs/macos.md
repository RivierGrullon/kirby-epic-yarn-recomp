# macOS Intel and Apple Silicon development

macOS support is experimental. The build entry point supports **native x86_64
on Intel** and **native arm64 on Apple Silicon**, with Metal graphics. Neither
architecture is declared gameplay-validated by the presence of a passing CI job.
The existing Linux and Windows release installers remain the published packages.

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

## Build

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
higher minimum OS. Intel uses the runtime's x86-64-v2 profile; Apple Silicon
uses the M1 instruction baseline rather than tuning for a newer build host.
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
Mac Setup installer that builds from each user's own disc, including automated
toolchain downloads, is a separate follow-up. Developer ID signing and
notarization are also not supplied by this patch. There is no universal binary
or claim of cross-architecture gameplay compatibility yet.

## Validation before a release

The macOS workflow uses separate Intel and ARM runners. It compiles/runs the
pinned runtime's host tests, Kirby's NAND/disc and process tests, the native
launcher and the Qt frontend, without any game assets. It uploads no game build.

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
