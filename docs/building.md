# Building and runtime diagnostics

Run these commands from the repository root. See [the project README](../README.md)
for player instructions and current limitations.

Initial static recompilation work for **RK5E01 Rev 0 (USA)** on Linux x86-64.

The repository directory is named `kirby`; run the commands below from its root.
Owned runtime code uses public headers under `runtime/include/kirby/`, implementations
under `runtime/src/`, and audited SDK replacements under `runtime/native-src/`.
Each is grouped by function. CMake integration lives in `cmake/`, upstream adaptations
in `patches/wiicompiled/`, and revision audits in `tools/audits/`. Tests are split into
`tests/unit/`, `tests/integration/` and `tests/tooling/`.
See [runtime layout](../runtime/README.md) and [additional references](references.md).

The target test device is a Lenovo Legion Go running Bazzite. The intended first playable
target is the menu, Fountain Gardens, and Kirby movement with a controller. The current
Vulkan startup test ran on an Acer Nitro AN515-55 with an NVIDIA RTX 3050 Laptop GPU;
Legion Go graphics/controller validation is still pending.

**Current state:** the native executable renders the main menu, loads File 1,
completes Patch Castle and starts Fountain Gardens. Kirby movement in both directions
and jumping in Fountain Gardens are verified with virtual PAD input. An independent
native restart loaded the updated File 1 into Quilty Square with the unlocked house.
Physical DualSense/Legion Go input remains unverified; the user's nonresponse and
reversed-direction reports are unresolved.
The executable is `build/native/WiiCompiled`.
`--video-hidden` enables native Vulkan diagnostics without showing a window; it
was used for the Patch Castle and Fountain Gardens checks. It requires a working
host graphics session. `--headless` is the separate non-graphical probe.

WiiCompiled generates 32,737 C++ functions. The build links 32,714 shard functions,
the original DSP task-loader body retained behind its wrapper, and 23 audited native
overrides. Startup reaches `Game::Run`, starts the Good-Feel and sound worker threads,
loads the verified AXWii task, delivers original SDK sleep alarms, and reads extracted
DATA files. Native WPAD reports and asynchronous controller-information callbacks pass
integration checks. Physical controller movement remains unverified.

The graphics adapter handles direct and bounded indexed vertices, RAM textures and palette TMEM
uploads. Textured UI draws execute, GPU copies write 640x480 YUYV buffers in MEM2, and VI
presents those buffers. EFB contents survive host readback and presentation boundaries,
including copies without a guest clear. Color/depth EFB-to-texture readback and half-scale copies pass GPU probes. Indexed
NBT normals and additional display scaling/gamma/field modes remain unsupported. GPU readback now
polls Dawn on Aurora's render worker; previous concurrent polling produced driver crashes
and timeouts. The updated path passed the synthetic GPU readback/presentation test and
sustained native runs. Stage 30 traced the reproduced shutdown heap corruption to
audio teardown after SDL shutdown and corrected the resource destruction order.

AXWii task boot and a 96-sample mixer probe pass. The native 32 kHz audio DMA sink opens;
MEM2 sample mapping and overdue refill ordering are repaired. A hidden Quilty Square
test reaches approximately 333.33 mixer outputs/s. Queue overflow has been observed;
audible tempo, synchronization and sustained playback still need validation.
Local NAND supports file/directory creation, file access, deletion of files/empty
directories, and directory usage queries. Creation metadata persists in host xattrs;
attribute queries and file commits now let the game complete initial save creation. General IOS support, recursive deletion, arbitrary context restoration and
unmatched preempted contexts remain unsupported. Native IRQ preemption is supported
at the audited interrupt-enable boundary. A USB DualSense is detected at boot. The latest run
passes indexed rendering and a real 320x240 RGBA8 EFB copy. Main-menu navigation,
File 1 startup, Patch Castle completion and Fountain Gardens movement are verified
with virtual input. Physical input remains pending. Independent save reload is verified.
The latest progression save is in an isolated ignored test directory; the ordinary
launch save was preserved. All native game test processes have been stopped.
See [bring-up results](bringup.md) for commands and evidence.

The game is recompiled to C++ and built with Clang. Python prepares inputs, .NET runs
WiiCompiled, and Dolphin extracts the disc. Dolphin is not the native execution path.

## Local setup

Run these commands from this repository. Source dependencies are pinned in
`dependencies.json`; their original licenses remain in their checkouts.

```bash
python3 tools/bootstrap.py
bash tools/container.sh --build
bash tools/container.sh python3 tools/kirby.py doctor
```

The rootless Podman image contains .NET 8 SDK, Clang, CMake, Ninja and Linux runtime
development dependencies. It does not install packages on the Bazzite host.
The .NET base image is pinned by digest; Ubuntu package repositories are not snapshotted.
Building it requires network access and several GB of local storage. The container
mounts only this repository; put extracted game data under `disc/` or `local/` for builds.

## Extract and verify your own copy

Dolphin's `dolphin-tool` and 7-Zip run on the host. The extraction helper supports
the installed Dolphin Flatpak and preserves the complete DATA partition, including `files/`.

```bash
python3 tools/extract.py "/run/media/system/Games/Kirby's Epic Yarn (USA) (En,Fr,Es).7z"
bash tools/container.sh python3 tools/kirby.py prepare
```

An individual `.wbfs`, `.iso`, or `.rvz` can also be passed to `extract.py`.
For an existing extracted folder, use `kirby.py prepare --dol /path/to/sys/main.dol`.
That path must be accessible inside the container when using `container.sh`.

The helper refuses a partial extraction in a nonempty `disc/` directory. Move it aside
before retrying. A completed extraction is reused after verifying its DOL hash.

Required SHA-1: `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`.
Other revisions are rejected. The preparation step also checks executable sections,
the startup call, r1/r2/r13 initialization, every mapped function range, and the checked-in
profile constants. It writes the local profile and evidence to `local/`.

## Translate and attempt the native build

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 2
bash tools/container.sh python3 tools/build_native.py --jobs 2
```

The build helper snapshots the upstream runtime and pinned Aurora submodule, then applies
the checked-in patches to those local copies. The C++ bootstrap selects the verified Kirby entry,
rejects foreign runtime registrations and avoids Mario Kart's constructor/bootstrap path.
The default mode is a headless diagnostic; `--video` enables the Vulkan window for
graphics bring-up. Kirby-specific hardware/HLE integration is incomplete.
The helper never launches its output.

Logs are in `build-logs/`. The build stops on command failure and retains its log.
`--configure-only` on `build_native.py` skips compilation. Two build jobs limit memory
pressure from generated C++ shards.

```bash
bash tools/container.sh build/native/WiiCompiled --verify-bootstrap
bash tools/container.sh build/native/WiiCompiled --probe-init
bash tools/container.sh build/native/WiiCompiled --probe-interrupt-init
bash tools/container.sh build/native/WiiCompiled --probe-exi-init
bash tools/container.sh build/native/WiiCompiled --probe-si-init
bash tools/container.sh build/native/WiiCompiled --probe-video-retrace
bash tools/container.sh build/native/WiiCompiled --probe-sram-init
bash tools/container.sh build/native/WiiCompiled --probe-boot-memory
bash tools/container.sh build/native/WiiCompiled --probe-audio-clock
bash tools/container.sh build/native/WiiCompiled --probe-dsp-init
bash tools/container.sh build/native/WiiCompiled --probe-ipc-client
bash tools/container.sh build/native/WiiCompiled --probe-stm-init
bash tools/container.sh build/native/WiiCompiled --probe-system-config
bash tools/container.sh build/native/WiiCompiled --probe-disc
bash tools/container.sh build/native/WiiCompiled --probe-es-metadata
bash tools/container.sh build/native/WiiCompiled --probe-ave
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

The verification commands pass. The final headless startup command exits 1 when GX
scissor setup requires an active render target; use `--video` to continue through that
point. The disc probe verifies 16 reads of the disc ID, FST and banner, including original
callback delivery with interrupts enabled and deferred delivery with interrupts masked.
`--game-data DATA` overrides the default `disc/DATA` path. The bootstrap
initializes Aurora only for `--video`. Host audio playback and general guest scheduling
remain unfinished. It loads
the embedded DOL, maps retail MEM1/MEM2 with their physical/cached/uncached aliases,
loads disc boot metadata, and lets the compiled game execute its own startup. Runtime
boot requires the complete extracted DATA folder, including `sys/main.dol`, `sys/boot.bin`,
`sys/bi2.bin`, `sys/apploader.img`, `sys/fst.bin`, `ticket.bin`, `tmd.bin` and every FST file under `files/`;
translation alone still needs only `sys/main.dol`. Unmapped hardware is reported rather
than replaced with zero-filled pages.

For an address audit without enabling hooks:

```bash
bash tools/container.sh python3 tools/audit_hooks.py
```

The TSV report stays in `local/hook-audit.tsv`. A matching symbol is an address candidate;
its ABI, data dependencies and implementation still need review.

## Symbol map

```bash
python3 tools/kirby.py map
```

`tools/kirby.py` verifies the expected hash against Swiftshine/key's
`config/RK5E01/config.yml`, reads code section declarations from `splits.txt`, and selects
only `type:function` records from `symbols.txt`. It preserves names, keeps the first
alias at each address, and sorts by address. Each output line is `80004050 __start`:
eight hexadecimal digits **without** `0x`, followed by the symbol name. This matches
WiiCompiled's `FunctionMap.Parse`. The resulting 32,697 unique function boundaries
seed discovery; translation additionally discovers 40 function starts.

## Verify the preparation tools

```bash
python3 -m unittest discover -s tests -v
# The same tests inside the build environment:
bash tools/container.sh python3 -m unittest discover -s tests -v
# Include the sixteen native integration tests after building with your DOL:
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
# Standalone register and flat-memory tests: no game data required.
bash tools/container.sh cmake --build build/native --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test kirby_ipl_test kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test kirby_dsp_boot_test kirby_ios_service_test kirby_nand_test kirby_ios_nand_test kirby_disc_test kirby_es_metadata_test kirby_ave_gpio_test --parallel 2
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
```

Tests use synthetic bytes, including signed `addi` and unsigned `ori` SDA initialization,
wrong revisions, truncated sections, invalid memory ranges and function/data symbol separation.
The native checks reject foreign overrides, execute Kirby's real register initializer, and
verify that invalid arguments exit without shutting down an uninitialized renderer.
The interrupt initializer probe supplies its independently verified exception-table
precondition, executes Kirby's compiled initializer, and checks its installed handler
and device masks. The EXI probe then executes Kirby's actual initializer and checks
its return, deselected channels, completed transfers and diagnostic output. The register
test covers interrupt propagation, boot configuration and rejected operations; the separate flat-memory test covers dynamic MMIO routing,
unsupported-read rejection and direct big-endian RAM access.

`runtime/src/devices/interrupts.cpp` implements a reviewed interrupt-register subset for PI,
MI, DSP, AI, EXI and the Broadway IPC IRQ registers. Masks, pending source propagation
and acknowledgments retain state. It also supplies EXI immediate transfers for explicitly
absent accessories, IPL command framing, a synchronous EUART diagnostic sink and the
read-only DI configuration register. SRAM/RTC reads and writes support immediate transfers and bounded DMA into verified
MEM1. Settings persist in ignored `local/user/sram.bin` (64 bytes); the RTC itself is
volatile and samples the existing runtime clock at command boundaries. ROM/font reads,
partition changes, general DSP microcodes and arbitrary guest exception delivery
remain unsupported. `runtime/src/devices/dsp.cpp` handles the verified DSP initialization
microcode, its reset/mailbox protocol, validated ARAM DMA and the AXWii task mailbox. It checks the uploaded
128 bytes against the DOL-loaded source before returning the initialization message.
The flat-MMIO patch routes dynamic device reads through the same checked handlers.

`runtime/src/services/ios_service.cpp` implements STM open/close, pending EVENTHOOK and
RELEASE_EH requests, file operations, and ES title/data-directory queries.
`runtime/native-src/services/ios.cpp` connects them to the original SDK wrappers and frees
requests with Kirby's own `iosFree`. Native callbacks run at explicit call boundaries
with an isolated register context and respect the guest's interrupt-enable bit. Hardware
IPC reply IRQs, general guest scheduling and controller event sources are still pending.
`--probe-stm-init` verifies initialization, 256 open/close cycles, cancellation and the
original event callback. `--probe-system-config` checks that the original SDK loads and
parses both configuration files through six asynchronous callbacks. New native overrides
require full translation to remove previously inlined copies:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh build/native/WiiCompiled --probe-stm-init
bash tools/container.sh build/native/WiiCompiled --probe-system-config
```

`runtime/src/services/nand.cpp` stores settings under ignored `local/user/nand/`, preserves
existing files and rejects paths that traverse host symlinks. Initial settings are
synthetic USA/English/NTSC, stereo, widescreen and progressive preferences. They do not
establish a rendered display mode. The boot loader also reads `sys/apploader.img` to
recover its minimum IOS version for the game's original compatibility check. Translation
alone still needs only `sys/main.dol`; native startup needs the extracted metadata.

Game images, extracted files, generated code, profiles containing local paths, binaries,
logs and dependencies are ignored by Git. Never commit them, including generated data
initializers that embed disc contents. Project tooling is licensed under [GPL-3.0](../LICENSE).
See [third-party attribution](../THIRD_PARTY.md).


`runtime/src/devices/video_si.cpp` models NTSC VI register state, beam timing, interrupt
comparators and SI polling. The four GameCube ports are explicitly disconnected and
return NOREP/COMERR instead of fabricated controller data. The host clock advances
this model at MMIO boundaries and during scheduler idle. Pending hardware IRQs also
run at the audited `OSRestoreInterrupts` boundary; queued SI callbacks need further
integration. Wii Remote input belongs to the
native WPAD bridge backed by Aurora. Core Remote reports and deferred guest callbacks
have an automated probe; physical controller mapping and gameplay remain unverified.
Framebuffer presentation has synthetic GPU coverage and real native game frames.
Menu, Patch Castle and Fountain Gardens milestones are recorded in the bring-up report.

## Native Vulkan startup

Run `--video` in the desktop session on the host; the build container deliberately
has no display sockets or GPU devices mounted. On the current Fedora/Bazzite host,
`ldd build/native/WiiCompiled` found only a bzip2 soname mismatch. The local compatibility
link below uses the installed library without changing system files:

```bash
mkdir -p local/host-libs
# Create once; reuse the existing link on subsequent runs.
ln -s /usr/lib64/libbz2.so.1 local/host-libs/libbz2.so.1.0
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 10s stdbuf -oL -eL \
  build/native/WiiCompiled --video
```

The current result is a Vulkan window, a completed XFB readback, three resumed retrace
waits, native WPAD initialization and exit 1 at the first BPE thread switch. This is unfinished startup, not playable
output. Dolphin is not running this test. Renderer caches stay in ignored
`local/user/aurora/`. A separate graphics probe checks real GPU copies and VI selection:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 15s \
  build/native/kirby_gx_renderer_test --gpu-xfb
```

The controller bridge has a separate headless probe:

```bash
bash tools/container.sh build/native/WiiCompiled --probe-wpad
```

It injects test reports through Aurora's PAD interface and verifies guest byte order,
report bounds, sampling-ring wrap, disconnection and masked callback delivery. It does
not establish physical SDL controller input or movement in Kirby. Extension reports
and additional WPAD commands remain further integration work.

The SDL event path has an additional isolated probe. It attaches a virtual SDL
gamepad, routes its actual SDL events through Aurora, and checks guest reports for
four directions, four face buttons, plus/minus, release and unplug while held for
standard, PS5, Xbox 360 and Xbox One presets. It does
not inject PAD status or open a window. Run it through the integration test so port
preferences and NAND initialization stay in a temporary directory:

```bash
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest tests.integration.test_native_bootstrap.NativeBootstrapTests.test_sdl_controller_events_reach_guest_reports_and_release_on_disconnect -v
```

This probe passes on the host and in the build container. Physical DualSense
nonresponse and reversed-direction behavior reported by the user remain unresolved;
an SDL virtual-device result cannot establish physical gameplay behavior.

To collect physical controller transitions without opening the game, run this from
the repository root and press/release left, right, up, down, cross and circle during
the 30-second capture. It uses fresh input defaults in a temporary working directory
and prints SDL, PAD and guest button masks, connection errors and capture/block state:

```bash
python3 - <<'PY'
import os, subprocess, tempfile
from pathlib import Path
root = Path.cwd()
env = os.environ.copy()
env['LD_LIBRARY_PATH'] = str(root / 'local/host-libs') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
with tempfile.TemporaryDirectory(prefix='kirby-input-') as cwd:
    result = subprocess.run([str(root / 'build/native/WiiCompiled'),
        '--monitor-controller', '--game-data', str(root / 'disc/DATA')],
        cwd=cwd, env=env, timeout=40)
    raise SystemExit(result.returncode)
PY
```

Exit status zero means the capture completed; check the transition count and reports
before drawing conclusions about physical input. This monitor does not exercise the
graphical window's focus/pause behavior or prove that Kirby moves.

The GPU probe can run without showing its window:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby_gx_renderer_test --gpu-xfb-hidden
```

It verifies the hidden window flag, XFB/texture/depth checks and 16 additional VI
frames. It also checks propagation of an SDL quit request. The latest run passed
298 checks. See Stage 27 in the bring-up record for
native game captures read from the guest XFB; Stage 28 records Fountain Gardens
gameplay. Those captures remain local/ignored.

The graphics probe uses synthetic colors, not game content. See the stage 16 report in
[bring-up results](bringup.md) for reproduction commands, limitations and logs.

The current controller mapping uses a horizontal Wii Remote layout: host south/east
are Wii 2/1 (DualSense cross/circle), with compensated D-pad directions. DPD control
is bridged to Aurora so connected-at-boot controllers avoid the original Bluetooth
queue. Physical mapping verification is still pending; virtual down/up correctly select
the main-menu entries.

Kirby supplies positional Xbox face-button defaults before PAD initialization.
The inherited GameCube Xbox preset swapped east/west, which sent Wii B instead of
Wii 1 from the east button. This mismatch was reproduced and corrected in Stage 29.
Saved per-device mappings still override defaults. This fix does not establish
physical DualSense behavior or Legion Go hardware compatibility.

Host exit events are propagated from both the video and controller event pumps.
The native process now releases its audio stream before Aurora shuts down SDL.
An optional graphics-session integration check boots the actual native executable
with a hidden window and verifies normal exit after SIGTERM:

```bash
KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest tests.integration.test_native_video -v
```

This check uses a temporary working directory and preserves the ordinary save and
controller preferences. It requires host Vulkan, audio and extracted game data.
