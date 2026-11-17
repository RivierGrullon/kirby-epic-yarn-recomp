# Build and play

You need your own **Kirby’s Epic Yarn, USA (RK5E01 Rev 0)** copy.

## Windows x64

### 1. Open PowerShell in the project folder

Use a Git checkout on Windows 10/11 (64-bit). Right-click the project folder
and choose **Open in Terminal**. You do not need to install Python, a compiler,
Visual Studio or Docker.

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
and Dolphin’s `dolphin-tool`, then run:

```bash
python3 tools/setup.py "/path/to/Kirby.wbfs"
./build/native/kirby-launcher
```

Docker or Podman must be running. The game itself runs on the host.

## Prerequisites

Windows tool downloads are automatic. Linux prerequisites, manual commands,
job limits, build staging and troubleshooting are in the
[advanced build guide](building-advanced.md).
