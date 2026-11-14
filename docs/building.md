# Building and updating

This is a source build for Linux x86-64 and Windows x64. Translation and compilation happen on
your machine using your own RK5E01 Rev 0 dump. Keep the complete extracted DATA
partition for runtime loading; translating `main.dol` alone is not enough to play.

## Windows x64

Use a 64-bit LLVM-MinGW Clang toolchain, Python 3.12 or newer, Git, .NET SDK 8,
CMake 3.25 or newer, Ninja, and Windows SDK C++/WinRT headers. Put `clang`,
`clang++`, `dotnet`, `cmake`, `ninja`, `git` and `python` on the current shell's
PATH. The tested environment used Clang 23.1.5, Python 3.14.6, CMake 4.3.4,
Ninja 1.13.2 and Windows SDK 10.0.26100.0. MSVC is not the exercised compiler.
Use NTFS for the writable runtime directory and a working hardware Vulkan driver.

From this repository's root in PowerShell:

```powershell
python tools/bootstrap.py
# Supply a complete DATA folder extracted from your own supported disc.
python tools/kirby.py translate --dol "D:/Games/Kirby/DATA/sys/main.dol" --threads 2
python tools/build_native.py --jobs 2 --translated-jobs 2
.\build\native\kirby-launcher.exe
```

Select the extracted DATA folder in the launcher. A direct launch uses:

```powershell
.\build\native\WiiCompiled.exe --video --game-data "D:/Games/Kirby/DATA"
```

These are source-tree commands, not a verified clean-machine installer. The
Windows exercise reused pinned dependency checkouts and an existing verified
extraction. The Linux `container.sh` and extraction helper are not required for
the native Windows build; extract with your own suitable DolphinTool separately.
`--dol` selects translation input and does not copy or relocate the DATA folder.

The build helper detects C++/WinRT headers in the installed Windows SDK and uses
directory junctions for its generated-source link, avoiding symlink privileges.
It selects Aurora's configured SDL target and copies runtime DLLs beside the
executables. Keep the source-tree layout; the launcher locates its root from
`build/native`. Close the game and launcher before replacing their binaries.

Game startup and launcher verification check the boot ID/revision, DOL size and
SHA-256 on Windows. Wrong inputs stop before translated game execution. Windows
NAND metadata is stored in NTFS named streams; preserve them when backing up
saves. Settings writes are flushed and atomically replaced on the same volume.

Build and run the project's unit targets:

```powershell
cmake --build build/native --target kirby_tests --parallel 2
ctest --test-dir build/native -R '^kirby_' --output-on-failure
python -m unittest discover -s tests -v
```

The `kirby_` filter excludes separate upstream `mkw_*` targets that the
`kirby_tests` build does not produce. See [Contributing](../CONTRIBUTING.md#tests)
for optional Windows executable checks. No game data is needed by the tooling
tests or synthetic storage fixtures.

## Linux prerequisites

The remaining container/extraction steps in this guide apply to Linux.

Install Git, Python 3, rootless Podman and Dolphin's `dolphin-tool` on the host.
The extraction helper also supports the Dolphin Flatpak. Install `7z` or `7zz`
if your WBFS is inside a `.7z` archive. A working desktop Vulkan driver is needed
to run the game and graphics tests.

The build container provides .NET 8, Clang, CMake, Ninja and Linux development
dependencies. It does not install packages on Bazzite's host filesystem. Network
access and several GB of free space are required. The base image is pinned by
digest; the distribution package repositories are not snapshotted.

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
It reuses a completed, verified extraction and refuses to overwrite an incomplete
nonempty `disc/` directory. Move a partial extraction aside before retrying.

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

## Translate and compile

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 2
bash tools/container.sh python3 tools/build_native.py --jobs 2
```

Use two jobs initially to limit memory pressure from generated C++ shards. Build
logs remain in `build-logs/`; a failed command stops the build. The helper prepares
the shared runtime, applies the checked-in patches and builds `WiiCompiled` and
`kirby-launcher`. It does not launch the game.

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
