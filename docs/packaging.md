# Application packages

The application package is separate from the developer checkout. Its setup tools
belong to the package; players should not install Python, Git, a compiler, or a
container engine. Selecting a supported game source starts extraction,
translation, compilation and silent native startup checks. A matching verified
build is reused. Later launches use the selected native installation.

This packaging work is experimental. An AppImage format smoke check is not an
end-to-end installation test, and does not establish gameplay performance.
Windows portable ZIP preparation and validation are still outstanding.

## Linux AppImage

The image includes the launcher, an isolated setup worker, PRoot and a private
Linux build environment with DolphinTool. PRoot is used for build and extraction
commands. The resulting game and its startup checks run on the host.
Build subprocesses use a private runtime directory inside the writable temporary
storage, so .NET mutexes do not depend on the host desktop's `/run/user` paths.

The AppImage mount stays read-only. Authored sources are prepared under:

```
$XDG_DATA_HOME/kirby-native/versions/<source-commit>/
```

If `XDG_DATA_HOME` is unset, it defaults to `~/.local/share`. Generated code,
compiler caches, installations, settings and saves stay under that writable
directory. Opening the same source version again preserves those files. Older
version directories are retained; migration between source versions is not yet
automatic. The developer checkout and its saves are not imported or modified.

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
podman build -t localhost/kirby-portable-tools:dev \
  -f tools/Containerfile.portable tools

# Rebuild the launcher after committing the packaging sources.
bash tools/container.sh cmake --build build/native --target kirby-launcher --parallel 2

python3 tools/package_portable.py --output local/releases/Kirby.AppDir
python3 tools/build_appimage.py local/releases/Kirby.AppDir \
  --output local/releases/Kirby-linux-x86_64.AppImage
```

The packager exports the private toolchain and freezes the setup worker. It
creates a fresh shallow source snapshot, and includes only the launcher from the
native build. It excludes the disc, translated game code, native game executable,
settings, saves and developer caches. Existing component notices stay with their
components. The generic AppImage loader comes from the official
[AppImage/type2-runtime](https://github.com/AppImage/type2-runtime) project and is
checked against the SHA-256 pinned in `tools/build_appimage.py`.

### Verify outside the checkout

Use a new data directory and an empty tool search path. The first command checks
the real launcher without opening a visible window:

```sh
XDG_DATA_HOME="$PWD/local/package-test-data" PATH=/nonexistent \
  SDL_VIDEO_DRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy \
  local/releases/Kirby-linux-x86_64.AppImage --appimage-extract-and-run --self-test
```

Then open the AppImage normally, choose your own DATA folder or disc image, and
wait for the complete native installation. Check the selected build, start the
game, close it, and reopen the AppImage to confirm the build is reused. Test a
second location with spaces in its path. Keep all game outputs and recordings
local. A public release needs these checks on clean systems as well as normal
gameplay and controller testing.
