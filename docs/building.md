# Building and updating

This is a source build for Linux x86-64. Translation and compilation happen on
your machine using your own RK5E01 Rev 0 dump. Keep the complete extracted DATA
partition for runtime loading; translating `main.dol` alone is not enough to play.

## Prerequisites

Install Git, Python 3, rootless Podman and Dolphin's `dolphin-tool` on the host.
The extraction helper also supports the Dolphin Flatpak. Install `7z` or `7zz`
if your WBFS is inside a `.7z` archive. A working desktop Vulkan driver is needed
to run the game and graphics tests.

The build container provides .NET 8, Clang, CMake, Ninja and Linux development
dependencies. It does not install packages on Bazzite's host filesystem. Network
access and several GB of free space are required. The base image is pinned by
digest; the distribution package repositories are not snapshotted.

Build commands use the host network namespace so the container can reach the
host's DNS resolver on Bazzite. The container still receives no display sockets,
audio sockets or GPU devices. Compilation does not start the game.

## Fetch sources and build tools

```bash
git clone https://github.com/RivierGrullon/kirby-epic-yarn-recomp.git
cd kirby-epic-yarn-recomp
python3 tools/bootstrap.py
bash tools/container.sh --build
bash tools/container.sh python3 tools/kirby.py doctor
```

If you already have a checkout, start from its root instead of cloning again.
`dependencies.json` pins each source revision. Bootstrap refuses to overwrite a
different or modified checkout. It retains upstream license files under `vendor/`.

Aurora is fetched at `vendor/wiicompiled/aurora-main`. The build helper creates
the patched build copy at `local/native-workspace/aurora`; authored patches live
in `patches/aurora/`. Do not edit the dependency checkout to make project changes.

## Extract and verify your copy

```bash
python3 tools/extract.py "/path/to/your-game.wbfs"
bash tools/container.sh python3 tools/kirby.py prepare
```

The helper also accepts `.iso`, `.rvz`, or a `.7z` containing exactly one WBFS.
It extracts the full DATA partition under `disc/DATA` and verifies the revision.
It refuses to overwrite a nonempty `disc/` directory, including one with an old
completion marker. That marker does not identify a newly selected image. Move
the directory aside only when you want a fresh export; existing play data can
remain where it is.

For a folder already extracted with Dolphin, place the complete DATA contents at
`disc/DATA/`, including `sys/`, `files/`, `ticket.bin` and `tmd.bin`, and run
`prepare`. For translation-only inspection, `prepare --dol PATH` accepts another
DOL path accessible inside the build container; it does not relocate runtime data.

Only **RK5E01 Rev 0, USA** is supported. Required `sys/main.dol` SHA-1:

```text
f4280a2b0709bfde4132be22e9f7fa8ecfa2a130
```

Preparation checks this against Swiftshine/key, validates executable sections and
function ranges, and derives the startup registers from the DOL. Expected values
are entry `0x80004050`, r2 `0x808EE8E0` and r13 `0x808E73E0`.

### Inspect another source without replacing the current game

```bash
python3 tools/install.py --inspect "/path/to/your-game.7z"
python3 tools/install.py --inspect "/path/to/extracted/DATA"
```

The setup worker accepts DATA folders, Dolphin export parents, WBFS, ISO, RVZ
and a 7z containing exactly one WBFS. Image inspection exports the full DATA
partition into a temporary directory, verifies it, then selects a cache under
`local/setup/sources/`. It can take as long as extraction. The completed event's
`result.data_root` is the verified folder. It does not replace `disc/`, change
the launcher's selected data, translate, compile, or start the game.

Reusing an image cache requires hashing the selected image again and rechecking
the exported layout. Validation covers the exact DOL hash, Wii/revision and title
metadata, and every FST entry's path, file size and logical extent. It does not
authenticate Nintendo signatures or all asset contents. Keep your dump intact.

The launcher exposes this import under **Install → Choose disc image** or
**Choose DATA folder**. It shows real byte progress while hashing and the current
stage while unpacking, extracting or validating. **Cancel import** stops active
work. Closing the launcher during import requests cancellation and waits for the
worker to exit. Play and additional imports are disabled during the job.

The selected DATA path changes only after a valid completion and successful
worker exit. Failure, cancellation and picker dismissal keep the previous
selection. **Verify selected folder** runs the same full layout check. Imported
sources stay under `local/setup/`; saves and executable files are not moved.

The worker emits JSON lines; external tool output stays in `local/setup/logs/`.
Only one job can use a setup workspace at a time. SIGINT/SIGTERM cancel active
work and remove its temporary export. See [the worker protocol](../tools/installer/README.md)
for launcher integration. Translation, compilation, repair and executable
selection from the Install page remain under development. A source import is not
a newly compiled build.

To test the launcher's actual process/protocol client without opening a window
or changing the selected data or settings:

```bash
build/native/kirby-launcher --check-source "/path/to/own-disc.wbfs"
```

## Translate and compile

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 2
bash tools/container.sh python3 tools/build_native.py --jobs 2
```

Use two jobs initially to limit memory pressure from generated C++ shards. Build
logs remain in `build-logs/`; a failed command stops the build. The helper prepares
the shared runtime, applies the checked-in patches and builds `WiiCompiled` and
`kirby-launcher`. It does not launch the game.

### Build separately from the runnable version

Both helpers accept `--stage-root` for setup work. Use a directory below the
reserved `local/setup/builds/` subtree:

```bash
stage_root="local/setup/builds/manual-01"
bash tools/container.sh python3 tools/kirby.py translate \
  --stage-root "$stage_root" --dol disc/DATA/sys/main.dol --threads 2
bash tools/container.sh python3 tools/build_native.py \
  --stage-root "$stage_root" --jobs 2 --translated-jobs 2
```

The stage retains `metadata/` (profile, function map and DOL audit), `generated/`,
`translator/bin/`, patched `native-workspace/`, `build/` and separate `logs/`.
Authored sources and pinned dependencies are read from the checkout. Translation
and compilation do not replace the developer `generated/` tree or `build/native`
executables. The ownership record refuses a nonempty, unowned stage or one from
another checkout; user directories and the developer build cannot be stage roots.
The final directory component is a job ID of 1–80 ASCII letters, digits,
underscores or hyphens. This is an internal build-tool path requirement: Ninja's
dependency parser can lose filenames containing apostrophes and rebuild them
on every invocation. Original image/archive filenames are preserved. Use the
container workflow above so compiler source/output paths use `/workspace`.

Staged .NET compilation uses a private temporary artifacts directory and retains
the runnable translator binaries in the stage. This avoids .NET 8 item-transform
failures with apostrophes in output paths, while keeping dependency `bin/` and
`obj/` trees unchanged. Later translation commands may use
`--translator-dll "$stage_root/translator/bin/Translator.Cli/release/Translator.Cli.dll"`
to reuse that explicitly built tool.

The staged native game is `"$stage_root/build/WiiCompiled"`. Run bootstrap checks
with an explicit `--game-data DATA` and keep diagnostic user data separate.
Staging alone does not install or select the new executable. The normal launcher
stays at `build/native/kirby-launcher`; build coordination, native verification,
installation manifests and atomic executable selection are available through the
host-side worker:

```bash
python3 tools/install.py --install "/path/to/extracted/DATA"
```

This requires the pinned dependencies and existing toolchain image prepared
above. It builds in a cache, verifies a separate native copy with silent hidden
startup probes, and writes a verified bundle and atomic selection pointer under
`local/`. It preserves the developer executable and user settings/saves.
The current launcher does not yet read that pointer or offer compilation,
repair and rollback buttons. See [Native installation](../tools/installer/README.md#native-installation)
for result fields, cache behavior and remaining integration limits.

## Run on the host

```bash
build/native/kirby-launcher
```

Run from the repository root and keep the launcher beside `WiiCompiled`. Select
`disc/DATA` in the launcher. To launch directly:

```bash
build/native/WiiCompiled --video --game-data disc/DATA
```

The build container intentionally has no display sockets or GPU devices mounted;
run visible graphics on the host. This is not yet a portable packaged build.

For a silent, unmapped Vulkan diagnostic, launch on the host with
`SDL_AUDIO_DRIVER=dummy` and `--video-hidden --volume 0`. A virtual test gamepad
can deliver background events when `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` is
set for that process. This does not change normal launcher input behavior.
Keep diagnostic saves separate from normal play; hidden results do not certify
visible pacing or audible synchronization. See [performance results](performance.md#hidden-native-gameplay-and-ten-minute-movement-check).

The launcher and game share [versioned settings](settings.md), including the DATA
path, master volume, VSync and experimental copy mode. Settings changes do not
require a build. Existing `launcher.cfg` preferences migrate on launcher startup.

If the dynamic loader reports a missing library, inspect `ldd build/native/WiiCompiled`
and `ldd build/native/kirby-launcher`. On the tested Bazzite host the missing
`libbz2.so.1.0` was a soname difference. If `/usr/lib64/libbz2.so.1` exists, a local
compatibility link can be used without changing system files:

```bash
mkdir -p local/host-libs
test -e local/host-libs/libbz2.so.1.0 || ln -s /usr/lib64/libbz2.so.1 local/host-libs/libbz2.so.1.0
LD_LIBRARY_PATH="$PWD/local/host-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" build/native/kirby-launcher
```

Do not use that link for unrelated missing libraries. See the
[README](../README.md) for controls, settings, saves and known limitations.

## Rebuild after changes

For authored C++ or launcher changes, rebuild before relaunching:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled kirby-launcher --parallel 2
```

The launcher runs the existing executable; it does not compile changes. After
changing native registrations or translation inputs, run translation and
`build_native.py` again. After changing upstream patches, use `build_native.py`
to check and apply them to the local snapshots.

## Identify the executable being used

Diagnostics shows separate build records for the launcher and its sibling
`WiiCompiled` executable. Each record contains the project revision, whether the
checkout was modified at build time, an authored-source fingerprint, dependency
pins and CMake compiler configuration. Expand the details for full identifiers.
Refresh queries the existing game executable; it does not rebuild or launch play.
An older executable without this query remains launchable and reports unavailable
build information.

The same records are available without game data, settings or SDL device startup:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby-launcher --build-info
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --build-info
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/kirby-launcher --check-native-build "$PWD/build/native/WiiCompiled"
```

Output is versioned TOML. The launcher reads the actual child's response with a
2.5-second timeout and a 16 KiB output limit. Query subprocesses use offscreen
video and dummy audio drivers, without changing the launcher's environment or
user preferences. Missing executables, unsupported queries and malformed records
produce a status message in Diagnostics.

CMake runs `tools/build_info.py` before building the shared metadata library.
The generator requires a Git checkout root with a commit. It hashes tracked and
untracked, nonignored project files, recording symlink targets without following
them. Disc files, generated game code, dependency snapshots, builds, caches and
local settings remain excluded by this repository's ignore rules. Dependency
fields record the declared pins. Binary checksums and installation verification
are separate checks; this fingerprint does not inspect ignored inputs.

The generated header stays under `build/native/kirby-generated/`. Unchanged
content keeps its timestamp, and only a standalone metadata translation unit
includes it. A version-record update therefore avoids rebuilding the translated
game shards. Runtime settings changes still require no compilation.

## Tests and revision audits

Tooling tests use synthetic fixtures and require no game:

```bash
python3 -m unittest discover -s tests -v
```

Native tests are opt-in and require the local build. Commands and GPU test
conditions are listed in [Contributing](../CONTRIBUTING.md) and
[Performance](performance.md). A graphics test passing does not prove gameplay
speed or physical-controller behavior.

```bash
bash tools/container.sh python3 tools/audit_hooks.py
```

The ignored `local/hook-audit.tsv` lists address candidates. Each override still
requires an ABI and state-dependency audit; addresses from other games are not
valid substitutes.

The symbol map is generated by `python3 tools/kirby.py map` from
`vendor/key/config/RK5E01/symbols.txt`. Only `type:function` records in code sections
declared by `splits.txt` are emitted. The map contains one uppercase hexadecimal
address (without `0x`) and symbol name per line, sorted by address; the first name
wins for duplicate addresses. `prepare` additionally checks ranges against the
actual DOL. The map and preparation evidence stay under `local/`.

Keep disc files, generated translations, data initializers, binaries, save data
and caches out of Git. They are intentionally ignored. Earlier device-level
investigations remain in [historical diagnostics](diagnostics.md).
