# Application packages

The native game and launcher run on **Windows and Linux (x86-64)**. Windows builds
produce a [ZIP](#windows-zip); Linux offers an [AppImage installer](#linux-appimage).

The application package is separate from the developer checkout. Its setup tools
belong to the package; players should not install Python, Git, a compiler, or a
container engine. Selecting a supported game source starts extraction,
translation, compilation and silent native startup checks. A matching verified
build is reused. Later launches use the selected native installation.

The sections below cover installation, package contents and maintainer build
instructions for each platform.

## Windows ZIP

`tools/windows/build-windows.ps1` creates `local/releases/Kirby-Windows-x64.zip` after a successful
build. Use `tools/windows/build-windows.ps1 -PackageOnly` to package existing binaries again.
The ZIP includes the launcher, game executable, runtime DLLs, DSP resource,
Microsoft runtime installer and component notices. It excludes disc assets,
settings, saves and compiler outputs. Extract it, install the included Microsoft
runtime if needed, then open `kirby-launcher.exe` and select your DATA folder.
The ZIP runs the precompiled game; extraction and rebuilding use the source
checkout. See the [quick guide](building.md#windows-x64).

## Linux AppImage

The AppImage opens **Kirby Setup**, a separate PyQt6 installer. Choose your own
disc image or DATA folder and an installation folder, then select Install. The
window shows the current phase, measured progress, elapsed time, expandable details
and cancellation. Once setup succeeds, use Open launcher or `Play Kirby.sh` in the
installation folder. The C++ launcher handles playing and settings; its Install
page opens Kirby Setup rather than running a compiler.

After a verified installation, setup adds **Kirby's Epic Yarn** to the current
user's applications menu (Start menu on Windows). Select **Also create a desktop
shortcut** to add it to the desktop as well. Setup uses the desktop's configured
folders, including localized or redirected desktop locations. Linux entries use
the installed `Play Kirby.sh`; Windows entries are native `.lnk` shortcuts to the
installed launcher. Reinstalling updates the menu entry to the latest completed
installation. A shortcut creation error is shown without disabling the installed
game or the Open launcher button.

The image includes Python, Qt, the authored launcher, an isolated console worker
and a pinned tools download descriptor. No host Python or Qt installation is needed.
Opening setup and playing an installed game do not
download tools. Importing a disc image or building a game downloads the private
Linux build environment with PRoot and DolphinTool once. PRoot is used for build and extraction
commands. The resulting game and its startup checks run on the host.
Build subprocesses use a private runtime directory inside the writable temporary
storage, so .NET mutexes do not depend on the host desktop's `/run/user` paths.

Tools are stored separately under
`$XDG_DATA_HOME/kirby-native/toolchains/<archive-sha256>/` (defaulting to
`~/.local/share/kirby-native/toolchains/`). Application versions reuse the same
tools when their descriptor pins the same archive. Downloads use the packaged CA
bundle, require HTTPS and match both the pinned byte size and SHA-256. Extraction
checks paths, links and expansion size before selecting a complete toolchain.
Cancelled or failed transfers leave no selected partial toolchain; retrying setup
starts a fresh transfer. A cache lock coordinates simultaneous application versions.

The tools archive contains the prebuilt pinned translator and the .NET runtime,
without the .NET SDK. Packaging-only PyInstaller files, PowerShell, caches and Git
metadata are excluded. Corresponding component source trees and license notices
remain in the tools archive. Archive timestamps are normalized so repackaging
identical tools preserves their download/cache identity.

Setup displays its current stage and elapsed time throughout installation. Download
and unpack progress use real byte totals; Ninja compilation uses completed build
tasks. Stages without a known total display an animated activity bar.

The AppImage mount stays read-only. The selected installation folder keeps authored
sources and native builds under:

```
<installation folder>/.kirby/<source-commit-prefix>/
```

The prefix is 12 hex digits and a marker retains the full source revision.
Generated code, compiler caches, installations, settings and saves stay under that writable
directory. Opening the same source version again preserves those files. Older
version directories are retained; migration between source versions is not yet
automatic. The developer checkout and its saves are not imported or modified.
The launcher and its host libraries are copied into this installation; it keeps
working after setup closes and its AppImage unmounts. Its setup button remembers
the original AppImage location. If that file is moved, open it manually to update
the recorded location. The old AppImage writable-version layout remains supported
by the worker's compatibility entry point.

Network access is needed for the pinned source dependencies and CMake downloads
on the first build. Installed graphics drivers and a supported desktop session
are still required. The first toolchain targets Linux x86-64 with glibc 2.39 or
newer, including the Bazzite development environment. Other systems need testing.

### Build a private test package

These commands are for maintainers. Start with the prerequisites and native
build cache described in [building.md](building.md), and commit the application
sources before packaging. The package refuses a launcher with different source
provenance.

```sh
podman build --network=host -t localhost/kirby-portable-tools:compact \
  -f tools/Containerfile.portable tools
podman build --network=host -t localhost/kirby-setup-tools:pyqt \
  -f tools/Containerfile.setup tools

# Rebuild the launcher after committing the packaging sources.
bash tools/container.sh cmake --build build/native --target kirby-launcher --parallel 2

python3 tools/package_portable.py --output local/releases/Kirby.AppDir \
  --tools-output local/releases/Kirby-tools-linux-x86_64.tar.xz \
  --tools-url https://github.com/OWNER/REPO/releases/download/TOOLS-VERSION/Kirby-tools-linux-x86_64.tar.xz
python3 tools/build_appimage.py local/releases/Kirby.AppDir \
  --output local/releases/Kirby-linux-x86_64.AppImage
```

Replace the example tools URL with the immutable HTTPS asset URL you will publish.
Publish the `.tar.xz` at that exact URL before distributing the AppImage. The
packager writes a companion `.tar.xz.json` descriptor containing its hashes, sizes
and translator revision. Keep it with the release artifacts. A later application
release can reuse that descriptor and archive without exporting the tools again:

```sh
python3 tools/package_portable.py --output local/releases/Kirby-next.AppDir \
  --tools-descriptor local/releases/Kirby-tools-linux-x86_64.tar.xz.json
```

Rebuild the tools when their dependencies change, using a new immutable URL.
The packager rejects descriptors with a different translator source revision.

The packager exports and reduces the private toolchain, then freezes the setup worker. It
creates a fresh shallow source snapshot, and includes only the launcher from the
native build. It excludes the disc, translated game code, native game executable,
settings, saves and developer caches. Existing component notices stay with their
components. The toolchain builds PRoot 5.5.0 from pinned upstream source; the
distribution's older 5.1 cannot restore archive symlink metadata through
`fchmodat2` on modern kernels. The generic AppImage loader comes from the official
[AppImage/type2-runtime](https://github.com/AppImage/type2-runtime) project and is
checked against the SHA-256 pinned in `tools/build_appimage.py`.

### Verify outside the checkout

Use a new data directory and an empty tool search path. The first command checks
the real launcher without opening a visible window:

```sh
XDG_DATA_HOME="$PWD/local/package-test-data" PATH=/nonexistent QT_QPA_PLATFORM=offscreen \
  local/releases/Kirby-linux-x86_64.AppImage --appimage-extract-and-run --self-test
```

Then open the AppImage normally, choose your own DATA folder or disc image, and
wait for the complete native installation. Check the selected build, start the
game, close it, and reopen the AppImage to confirm the build is reused. Test a
second location with spaces in its path. Keep all game outputs and recordings
local. A public release needs these checks on clean systems as well as normal
gameplay and controller testing.

## Standalone PyQt setup on Windows

The Qt frontend and JSON worker run on both platforms. On Windows, setup prepares
the existing pinned private tools through PowerShell and uses a native worker
adapter; it does not require a container engine. The shared cache is under
`%LOCALAPPDATA%/KirbyNative/toolchains/windows/<pin-prefix>`. The source pin is
checked in full even though the directory name is short. The cached Python reads
`KIRBY_PROJECT_ROOT`, so two installs do not share mutable module paths.

Build the Windows installer **on Windows** with native x64 Python (3.12 recommended),
the committed authored sources and a matching `build/native/kirby-launcher.exe`:

```powershell
python -m pip install -r tools/requirements-setup.txt pyinstaller==6.22.3
python tools/windows/package_setup.py --output local/releases/Kirby-Setup
```

This produces a ZIP containing `Kirby-Setup.exe`, its Python/Qt runtime, the console
worker, authored source snapshot and launcher DLLs. It excludes the translated
game executable, disc assets, generated code, saves and developer tools. Freeze
checks do not replace testing the real installer on a clean Windows machine.

For frontend development:

```sh
python3 -m pip install -r tools/requirements-setup.txt
python3 tools/setup_gui.py
QT_QPA_PLATFORM=offscreen python3 -m unittest discover -s tests -p 'test_setup*.py' -v
```
