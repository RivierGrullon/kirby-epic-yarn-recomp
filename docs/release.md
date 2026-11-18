# Build a player release

Build both installers from the same clean commit. Packaging requires only the checkout and packaging tools. Players supply their game data at install time, when setup builds the game and launcher together.

Publishing a GitHub Release or pre-release runs the **Release installers** workflow
for its tagged commit. Each successful platform job attaches its x86-64 installers
to that release. Linux provides an AppImage and a `.tar.gz` folder with an
`AppRun` entry point. Windows provides a `.tar.gz` folder with `Kirby-Setup.exe`.
The commands below are for manual builds.

## Windows x64

Run from the repository root in PowerShell with x64 Python and Git.

```powershell
$out = "local/releases/$(git rev-parse --short HEAD)"
python -m pip install -r tools/requirements-setup.txt
python tools/windows/package_setup.py --output "$out/Kirby-Setup-windows-x64"
$zip = Get-Item "$out/Kirby-Setup-windows-x64.zip"
$hash = (Get-FileHash $zip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $($zip.Name)" | Set-Content "$($zip.FullName).sha256" -Encoding ascii
```

## Linux x86-64 (WSL2 + Docker)

Use a Linux checkout with the setup Docker image and `mksquashfs` available. These commands reuse the published alpha.2 toolchain. For first-time image setup or a new toolchain, see the [packaging guide](packaging.md#build-a-private-test-package).

```bash
set -e
export KIRBY_CONTAINER_ENGINE=docker
out="local/releases/$(git rev-parse --short HEAD)"
mkdir -p "$out"
curl -fL -o "$out/tools.json" https://github.com/RivierGrullon/kirby-epic-yarn-recomp/releases/download/v0.1.0-alpha.2/Kirby-tools-linux-x86_64.tar.xz.json
python3 tools/package_portable.py --engine docker --output "$out/Kirby.AppDir" --tools-descriptor "$out/tools.json"
python3 tools/build_appimage.py "$out/Kirby.AppDir" --output "$out/Kirby-Setup-linux-x86_64.AppImage"
```

Smoke-test the extracted ZIP and the AppImage, then attach both installers and their matching `.sha256` files to the GitHub release.
