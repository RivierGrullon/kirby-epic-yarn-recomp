# Kirby's Epic Yarn Recompiled

**A native recompilation of Kirby's Epic Yarn for Linux and Windows x86-64, built with
[WiiCompiled](https://github.com/patchzyy/Wiicompiled) and
[Aurora](https://github.com/patchzyy/aurora-wiicompiled).**

The Wii executable is translated from PowerPC to C++ and compiled for the host.
The native runtime uses Aurora for graphics (Vulkan on Linux). Dolphin extracts your disc and serves
as a behavior reference; it does not run the native game.

![Fountain Gardens running in the native Linux build](docs/images/fountain-gardens.png)

*Fountain Gardens in the development build. A screenshot demonstrates rendering,
not stable frame pacing or full-game compatibility.*

> [!IMPORTANT]
> **Development source preview, not a finished release.** The main menu, Patch
> Castle and Fountain Gardens completion, plus save reload, have been
> observed. The full game, physical controllers and sustained performance still
> need validation. There is no ready-to-play binary package.
>
> You need your own **USA disc, RK5E01 revision 0**. Game files, generated game
> code, saves and shader caches are not included. Other revisions are rejected.

[Build from source](docs/building.md) · [Known limitations](#known-limitations) ·
[Gameplay checks](docs/gameplay-validation.md) · [Performance](docs/performance.md) ·
[Contributing](CONTRIBUTING.md) ·
[Changelog](CHANGELOG.md)

## Requirements

- Linux x86-64 or Windows x86-64 with a desktop session and working graphics drivers.
  Windows support is under development; see the build guide for validation limits.
- Your own complete RK5E01 Rev 0 dump, including the extracted `files/` tree.
- Linux: Git, Python 3.11+ and **Docker or Podman** for the build environment.
- Windows: the [native LLVM-MinGW toolchain](docs/building.md#windows-x64) used by celeron633, including Python 3.12+, .NET 8, CMake, Ninja and Windows SDK headers.
- Dolphin's `dolphin-tool` / `DolphinTool.exe` for image extraction; 7-Zip only for `.7z` inputs.
  Neither is required for an already extracted DATA folder.
- Several GB of storage and enough memory to compile the generated C++.

Expected `sys/main.dol` SHA-1:
`f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`.

The target handheld is a **Lenovo Legion Go with Bazzite**. Current graphics and
performance measurements were taken on an **Acer Nitro AN515-55 with an RTX 3050
Laptop GPU**. Legion Go performance has not yet been measured.
celeron633 exercised the Windows port on a Ryzen 5 9600X and RTX 3060. Startup and save
creation were observed, and the local tester reported that it works; level
completion, save reload and sustained Windows performance remain unverified.

## Build and play

Install the [prerequisites](docs/building.md#prerequisites), then run one setup
command from the checkout with the actual path to your own disc or DATA folder:

```bash
python3 tools/setup.py "/actual/path/to/Kirby.wbfs"
```

On Windows, follow [celeron633's native build steps](docs/building.md#windows-x64).
They build directly with LLVM-MinGW and do not require Docker or Podman.
If `disc/DATA` is already extracted, omit the path. Use `--engine docker` or
`--engine podman` to choose explicitly, or `--check` to check prerequisites first.
Setup prepares tools, translates, compiles and verifies the game and launcher.
See the [build guide](docs/building.md) for the full first-installation flow.

From a completed local build, run:

```bash
build/native/kirby-launcher
```

On Windows, open `build\native\kirby-launcher.exe`.

The launcher can currently:

- Import a disc image or select and verify an extracted DATA folder.
- Build and update the native game, with progress and cancellation.
- Start the native game in a visible window.
- Set master volume and VSync for the next launch.
- Rebind gamepad buttons, stick/trigger directions and keyboard keys, with
  conflict detection, swapping and reset.
- Enable **Experimental performance mode** to reduce GPU copy waits and repeated
  texture validation.
- Display detected controllers and open saves, logs and the build guide.

These actions are organized into **Play**, **Install**, **Display**, **Audio**,
**Controls**, **Saves** and **Diagnostics**. Navigation uses a sidebar on wider
windows and tabs on compact windows. Settings saving stays available on every
page. The Install page imports and verifies your source, then builds the native game.
First-time toolchain and launcher preparation is handled by `tools/setup.py`.

Settings are shared with the native game and saved on **Save settings**, **Play**,
or closing the launcher. Changes apply on the next game launch. Existing launcher
preferences migrate automatically while retaining the original file. See the
[settings guide](docs/settings.md) for the format and recovery instructions.
The first installation uses the setup command; later imports and builds can use
the launcher. Resolution
scaling, per-device input profiles and a validated 30/60 FPS
selector are not available in the launcher yet.

## Controls

Current default gamepad mappings:

| Gamepad position | Wii action |
| --- | --- |
| D-pad | Movement |
| South button / PlayStation Cross | Wii 2: confirm and jump |
| East button / PlayStation Circle | Wii 1: yarn action |
| Start / Options | Plus: menu |
| Back / Share | Minus: map or controls |

Default keyboard controls are arrow keys for movement, Space for jump/confirm,
Left Ctrl for yarn/cancel, Escape for Plus, Tab for Minus, X/Z for Wii A/B and F1
for Home. Open **Controls** to change these mappings. Gamepad bindings apply to
all players; keyboard bindings apply to player 1. See the
[settings guide](docs/settings.md#control-bindings) for capture and conflict handling.

Automated virtual SDL tests cover standard, PS5, Xbox 360 and Xbox One presets,
release and unplug behavior. Physical DualSense input and directional reports
still need resolution. These mappings are not a claim that every controller has
been tested. Per-device profiles, player assignment and physical
reconnection/cooperative play still need implementation or validation.

**Controls > Gyroscope & gravity / tilt** configures controller tilt, gyro or
right-stick aiming, sensitivity, inversion, deadzones and gyro calibration, with
a live sensor preview. There is no in-game settings overlay. Click the right
stick to recenter; optional keyboard motion uses Q/E to tilt and R to recenter.
Gyro mode falls back to the mouse when no gyro data is available. Physical motion
accuracy still needs testing on a supported controller; virtual sensors cover the
math and guest report path. See [motion settings](docs/settings.md#motion-controls).

## Known limitations

| Area | Current status |
| --- | --- |
| Game progression | Menu, Patch Castle and Fountain Gardens clears observed; whole-game completion unverified |
| Performance | Scene dependent; experimental optimizations are available, but stable 30/60 FPS is not guaranteed |
| Audio | Music tempo verified in a three-minute Fountain Gardens clip; long-session quality and broader synchronization remain unverified |
| Input | Virtual mappings tested; physical controllers and Legion Go controls need validation |
| Graphics | Native GX/VI rendering through Aurora; some formats and display modes remain unsupported |
| Saves | Creation/reload verified on Linux; Windows creation verified, reload pending |
| Distribution | Source-tree build and launcher; no portable installer or public binary release |

The window's FPS counter measures Aurora presentations. It does not establish
correct simulation speed or audio synchronization. See the
[measurements and test conditions](docs/performance.md) before comparing results.

## Saves and local files

When launched from the repository root:

| Data | Location |
| --- | --- |
| Game save | `local/user/nand/title/00010000/524b3545/data/` |
| Shared launcher/game settings | `local/user/settings.toml` |
| Previous launcher settings (retained after migration) | `local/user/launcher.cfg` |
| Shader cache | `local/user/aurora/` |
| Launcher game log | `build-logs/launcher-game.log` |

Back up saves before testing development changes. Launching the executable from a
different working directory uses a different local user directory.
Windows NAND attributes use NTFS named streams. Save backups must preserve those
streams; ordinary file contents alone do not retain Wii permission metadata.
Filesystems without named-stream support have not been validated.

## Documentation

- [Building and updating](docs/building.md)
- [Performance and audio findings](docs/performance.md)
- [Runtime organization](runtime/README.md)
- [Reference ports and reuse decisions](docs/references.md)
- [Bring-up evidence](docs/bringup.md) and [historical diagnostics](docs/diagnostics.md)
- [Contributing and reporting bugs](CONTRIBUTING.md)

## Credits and license

- **[celeron633](https://github.com/celeron633)** — Windows runtime contributions, widescreen presentation and controller motion support.
- **Swiftshine/key** — community decompilation, revision information and symbols.
- **WiiCompiled** — PowerPC translation and shared native runtime.
- **Aurora** — graphics and host integration.
- **WarioRecomp**, **New Super Paper Bros.**, **NSMBWiicompiled** and the other
  [reviewed ports](docs/references.md) — build and runtime references.
- **Dolphin contributors** — extraction tools and hardware behavior references.

Authored code is **GPL-3.0-only**; dependencies retain their own licenses. See
[LICENSE](LICENSE) and [third-party attribution](THIRD_PARTY.md).
Documentation screenshots show the original game running locally; the code
license does not grant rights to Nintendo's artwork or trademarks.

This is an unofficial project, not affiliated with Nintendo or Good-Feel.
