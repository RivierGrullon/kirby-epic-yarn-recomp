# Build and play

The native game and launcher target **Windows and Linux (x86-64)**. Follow the
instructions for your platform below.

You need your own **Kirby’s Epic Yarn, USA (RK5E01 Rev 0)** copy.

## Windows x64

### 1. Open PowerShell in the project folder

Use a Git checkout on Windows 10/11 (64-bit). Right-click the project folder
and choose **Open in Terminal**. Provide the [Qt frontend](#building-the-qt-desktop-launcher)
for the playable ZIP. The script provisions the native game build tools.

### 2. Run one command

Replace the example path with your WBFS file:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\build-windows.ps1 "D:\Games\Kirby.wbfs"
```

An extracted `main.dol` also works: replace the WBFS path with its path.
The script downloads the tools, checks your game, compiles it and creates a ZIP.
The first run needs internet and several GB of free space. Allow the Microsoft
runtime installation if Windows asks. Wait until you see **ZIP ready**.

### 3. Extract the ZIP and play

Find **`local/releases/Kirby-Windows-x64.zip`** in the project folder.
Extract the whole ZIP to a writable folder. On a new PC, run the included
`vc_redist.x64.exe` once, then open **`kirby-launcher.exe`**.

Under **Install**, choose your extracted **DATA** folder, then open **Play**.
For WBFS builds, the script prints the DATA folder location when it finishes.
The ZIP does not include your game assets or saves. A `main.dol` alone is enough
to compile, but playing requires the complete DATA folder.

### Package an existing build

To recreate the ZIP without compiling again:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\build-windows.ps1 -PackageOnly
```

Close the game before rebuilding. If a download or build fails, fix the reported
problem and rerun the same command; downloaded tools are reused. Build logs are
in `build-logs/`. Settings and saves in the extracted package are in `local/user/`;
keep this folder when updating.

## Linux

For the easiest setup, use the [AppImage](../README.md#linux-appimage).
To build from a Git checkout, install Git, Python 3.11+, Docker or Podman,
and Dolphin’s `dolphin-tool`. For the container-based source build, either
copy the matching Setup package's `tools/qt-launcher/` to
`build/native/qt-launcher/` or build the Qt frontend on the desktop host:

```bash
cmake -S launcher/qt -B build/desktop-ui -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/native/qt-launcher"
cmake --build build/desktop-ui
```

Then run:

```bash
python3 tools/setup.py "/path/to/Kirby.wbfs"
./build/native/kirby-launcher
```

Docker or Podman must be running. The game itself runs on the host.

## macOS (experimental)

Native Intel and Apple Silicon developer builds use [the macOS build guide](macos.md).
Use its dedicated entry point; the Linux container setup does not produce a Mac application.

## Prerequisites

Windows tool downloads are automatic. Linux prerequisites, manual commands,
job limits, build staging and troubleshooting are in the
[advanced build guide](building-advanced.md).

## Building the Qt desktop launcher

On Windows, install a Qt 6.4+ MinGW desktop kit and its matching MinGW compiler.
The native build discovers kits under `C:/Qt`; for another location, pass
`-DKIRBY_QT_PREFIX=<kit>` and `-DKIRBY_QT_CXX=<kit compiler>/g++.exe` when
configuring CMake. Linux source builders can install the Qt 6 Widgets
development package, Ninja and `qtpaths6`, or set `KIRBY_QT_PREFIX`.

Build the `kirby-launcher` target as usual. Its separate Qt project uses the
kit compiler and writes `qt-launcher/` beside the original launcher entry.
Keep this directory intact when copying a build. On Linux the Qt Widgets
runtime and its desktop platform plugin must be available on the host.
Release builds bundle Qt and its XCB plugin; the Linux desktop still supplies
its graphics and X11/XWayland libraries (including `libopengl0` and
`libxcb-cursor0` on Ubuntu).
Qt Widgets provides the launcher window. Without a Qt SDK you can use the
prebuilt `tools/qt-launcher/` directory from the matching Setup package:
copy the whole directory to `build/native/qt-launcher/` before running the
source setup or packaging a playable ZIP. Release installers deploy it
automatically. The native host's CLI and `--ui-service` work independently.
