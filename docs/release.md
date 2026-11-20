# Build a player release

Build both installers from the same clean commit. CI downloads a desktop Qt SDK
on each runner and builds `launcher/qt` directly, without disc files, the
translator or generated game code. It ships this C++ frontend with its private
Qt runtime. At install time the player's copy is used to build the game
(`WiiCompiled`) and the matching native launcher host (`kirby-launcher`).

Publishing a GitHub Release or pre-release runs the **Release installers** workflow
for its tagged commit. Each successful platform job attaches its x86-64 installers
to that release. Linux provides an AppImage and a `.tar.gz` folder with an
`AppRun` entry point. Windows provides a `.zip` archive with `Kirby-Setup.exe`.
Each installer includes the prebuilt frontend in `tools/qt-launcher/` and
has a companion `.sha256` file. Setup places the frontend beside the native
host in the player's installation.

## Reusing published Linux tools

Normal installer releases **do not rebuild or upload the Linux tools archive**.
`packaging/linux/toolchain-release.json` pins the published descriptor URL,
its SHA-256 and a fingerprint of the tool recipes, archive/export code and
translator source revision. The installer workflow verifies that pin before
packaging. Game, launcher and setup UI changes can reuse the same tools.
Windows setup continues using its existing independently pinned Windows tools;
the separate tools workflow below is for Linux.

If tracked tools inputs change, the Linux installer job fails with instructions
to renew the tools first. Keep older tools releases available: existing
installers still reference their immutable download URLs.

## Renewing Linux tools

Tools are attached to an **existing game release**, alongside its installers.
The workflow does not create releases or tags, change release notes, or change
which game release is marked Latest.

1. Select an existing mutable game release that already contains an installer,
   for example `v0.2.0-alpha.1`. Wait for **Release installers** to finish
   before targeting a newly published release. Existing tools remain available
   for older installers; new versions use distinct filenames and download URLs
   within the same release.
2. Open **Actions → Update Linux tools → Run workflow** on the desired source
   revision and enter that version in **release_tag**. The source revision is
   the branch selected for the run; the destination is the explicit game tag.
3. Leave **publish** disabled for an artifact-only test. The workflow builds when
   tracked inputs change. Select **force** to rebuild unchanged recipes, for
   example to pick up system package updates. Unchanged inputs without force
   skip the build. A forced build uses fresh Docker layers on the hosted runner.
4. Download the `linux-tools` workflow artifact and inspect the archive,
   descriptor, SHA-256 files and proposed `toolchain-release.json`. The workflow
   verifies the archive bytes and translator revision before generating this pin.
5. To attach the tools, run on the same source revision with **publish** enabled
   (and **force** if the inputs are unchanged). This performs a fresh build and
   uploads its verified files to **release_tag**. It checks the destination again
   before uploading and refuses to replace any existing tools files. Each run
   uses its unique run ID in the filenames, such as
   `Kirby-tools-linux-x86_64-123456789.tar.xz`. Start a new run for a new tools
   version; retrying the same run keeps its original filenames. Review the
   **uploading run's** artifact, since system packages can differ between runs.
6. Copy that run's `toolchain-release.json` to
   `packaging/linux/toolchain-release.json`, review it and merge the change.
   Subsequent player releases reuse those tools until another update is needed.

The tools workflow never commits a pin change automatically. Uploading tools
alone does not change installers until the pin is updated. The destination
release's existing installers continue using the pin they were built with;
subsequent installer builds can use the new pin.

If a change to the tools is blocking a new installer release, first attach the
new tools to an earlier mutable game release that already has installers.
Then merge its proposed pin before releasing the new installers. Keep every
referenced tools archive available so existing installers continue to work.

As described in [GitHub’s manual workflow documentation](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow),
the workflow must be present on the default branch to appear under Run workflow.

For a local tools-only build, prepare the native and portable images from the
[packaging guide](packaging.md#build-a-private-test-package), then run:

```bash
python3 tools/release_toolchain.py build --engine docker \
  --repository OWNER/REPO --tag v0.2.0-alpha.1 --asset-id tools-v2 \
  --output local/releases/new-tools
```

Use the tag of the existing game release that will receive the tools. This local
command creates the archive, descriptor, checksum files and proposed pin without
uploading, freezing an installer or packaging any game data. The workflow checks
that the destination exists and has an installer before building or uploading.

The commands below are for manual installer builds.

## Windows x64

Run from the repository root in PowerShell with x64 Python and Git.

```powershell
$out = "local/releases/$(git rev-parse --short HEAD)"
python -m pip install -r tools/requirements-setup.txt
python tools/deploy_qt_launcher.py --qt-prefix $env:QT_ROOT_DIR --compiler $env:CXX --output "$out/qt-launcher"
python tools/windows/package_setup.py --qt-frontend "$out/qt-launcher" --output "$out/Kirby-Setup-windows-x64"
$zip = Get-Item "$out/Kirby-Setup-windows-x64.zip"
$hash = (Get-FileHash $zip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $($zip.Name)" | Set-Content "$($zip.FullName).sha256" -Encoding ascii
```

## Linux x86-64 (WSL2 + Docker)

Use a Linux checkout with the setup Docker image and `mksquashfs` available. These commands reuse the verified tools release pinned in the checkout. For first-time image setup or a new toolchain, see the [packaging guide](packaging.md#build-a-private-test-package).

```bash
set -e
export KIRBY_CONTAINER_ENGINE=docker
out="local/releases/$(git rev-parse --short HEAD)"
mkdir -p "$out"
python3 tools/release_toolchain.py resolve --output "$out/tools.json"
python3 tools/deploy_qt_launcher.py --qt-prefix "$QT_ROOT_DIR" --output "$out/qt-launcher"
python3 tools/package_portable.py --engine docker --output "$out/Kirby.AppDir" --tools-descriptor "$out/tools.json" --qt-frontend "$out/qt-launcher"
python3 tools/build_appimage.py "$out/Kirby.AppDir" --output "$out/Kirby-Setup-linux-x86_64.AppImage"
```

Smoke-test the extracted Windows setup archive and the Linux AppImage, then attach the installers and their matching `.sha256` files to the GitHub release.
The **Release installers** workflow uploads them with the version in the name,
like `Kirby-Setup-0.1.5-linux-x86_64.AppImage` for tag `v0.1.5`, and writes each
checksum for that name. Do the same when attaching them by hand.
