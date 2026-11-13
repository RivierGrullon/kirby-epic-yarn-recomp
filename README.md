# kirby

A native Linux x86-64 port of **Kirby's Epic Yarn**, built by static recompilation
with [WiiCompiled](https://github.com/patchzyy/Wiicompiled) and
[Aurora](https://github.com/patchzyy/aurora-wiicompiled).

**Development build.** The native game reaches the main menu, completes Patch
Castle and loads Fountain Gardens. Movement and jumping there have been verified
with virtual controller input. The MEM2 audio sample mapping is repaired and real
game PCM is no longer silent. Native audio callback ordering now reaches the expected
333.33 mixer outputs/s in a hidden Quilty Square test. Physical controls, audio quality/synchronization and
sustained 30/60 FPS still need validation. See [performance findings](docs/performance.md).

## Requirements

- Your own extracted **RK5E01 Rev 0, USA** game, including the complete `files/` tree.
- Expected `sys/main.dol` SHA-1: `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`.
- Linux x86-64, a working Vulkan driver and a desktop session.
- The development build uses Python 3, rootless Podman, Clang, CMake and Ninja.

The target device is a Lenovo Legion Go with Bazzite. Current graphics tests ran
on an Acer Nitro AN515-55 with an NVIDIA RTX 3050 Laptop GPU. Legion Go performance
and controls have not been validated.

## Start the local build

After building, open **`build/native/kirby-launcher`**. The native C++ frontend can:

- Select an extracted DATA folder and verify its revision before starting.
- Start `WiiCompiled` with a visible game window.
- Set the master volume for the next launch.
- Toggle VSync to compare driver presentation behavior.
- Try **Experimental performance mode** to reduce texture and display-copy GPU waits.
- Show detected controllers and open saves, logs and the build guide.

The launcher uses the toolchain's existing SDL3 and Dear ImGui. Archive extraction,
toolchain installation, translation, compilation and release packaging are not yet
integrated into it. Use the [build guide](docs/building.md) for those steps. Keep
the launcher beside the executable in `build/native/`; this is a source-tree
launcher, not a portable release.

Experimental performance mode is applied on the next launch and saved with the
launcher settings. It reduced GPU readbacks in short Quilty Square comparisons;
Fountain Gardens movement and jumping also passed with virtual input. Whole-game
compatibility, audible quality and sustained 30/60 FPS remain unverified. Disable
the option and relaunch to compare with the synchronous copy path.
This optimization is separate from a frame-rate target. The 30/60 FPS selector
remains deferred until both modes preserve normal game and audio speed.

```bash
bash tools/container.sh cmake --build build/native \
  --target WiiCompiled kirby-launcher --parallel 4
build/native/kirby-launcher
```

For a first build, follow [extraction, verification and building](docs/building.md).

## Controls

| Gamepad position | Wii action |
| --- | --- |
| D-pad | Movement, with sideways Wii Remote compensation |
| South / DualSense cross | Wii 2: confirm, jump |
| East / DualSense circle | Wii 1: yarn action |
| Start / Options | Plus |
| Back / Share | Minus |

Virtual SDL tests cover standard, PS5, Xbox 360 and Xbox One presets, button release
and unplug while held. The reported physical DualSense nonresponse/reversed D-pad
behavior remains unresolved. Virtual input does not establish physical gameplay.

## Current limitations

| Area | Verified state |
| --- | --- |
| Native game | Menu, Patch Castle clear, Fountain Gardens movement/jump with virtual PAD |
| Saves | Creation and independent reload; test progression uses isolated copies |
| Frame rate | Variable; stable 30/60 FPS modes are not validated |
| Audio | MEM2 fetch and overdue callback ordering repaired; expected mixer rate measured; listening and synchronization pending |
| Controllers | Virtual presets pass; physical DualSense and Legion Go need validation |
| Graphics | Native Vulkan GX/VI; some formats and scaling modes remain unsupported |
| Shutdown | Startup/SIGTERM exits normally; extended gameplay cleanup is not fully tested |
| Scheduling | Native IRQ preemption at the audited SDK boundary; arbitrary context replacement remains unsupported |

The ordinary save is under `local/user/nand/title/00010000/524b3545/data`.
The game window title shows Aurora's rolling presentation FPS. This counter does
not certify simulation speed, audio synchronization or stable frame pacing.
Launcher settings are in `local/user/launcher.cfg`; game output goes to
`build-logs/launcher-game.log`. Development runs from another working directory
have separate saves. The launcher does not overwrite or import those copies.

## Documentation

- [Build and diagnostics](docs/building.md)
- [Performance and audio](docs/performance.md)
- [Runtime layout](runtime/README.md)
- [Reference ports and reuse decisions](docs/references.md)
- [Detailed bring-up evidence](docs/bringup.md)
- [Changelog](CHANGELOG.md)
- [Licenses and attribution](THIRD_PARTY.md)

Game files, assets, translated C++, binaries, saves, shader caches and captures stay
local and outside Git. Authored code is GPL-3.0-only; third-party components retain
their licenses and notices. This unofficial project is not affiliated with Nintendo
or Good-Feel.
