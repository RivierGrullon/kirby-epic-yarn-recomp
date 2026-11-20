# Third-party launcher integration

Checked against upstream documentation and source on 2026-11-19. A catalog listing,
download support, and a verified install-and-play flow are separate milestones.
The code in this change is not in v0.1.2; publish and test a new installer before
advertising its managed integration as available.

## Compatibility

| Launcher | Catalog requirements | Kirby-specific integration | Remaining verification |
| --- | --- | --- | --- |
| Quiver 4 | Submit the repository at https://quiverlauncher.com/submit. Platform release names and preferred executables are catalog metadata. | Use the Windows setup ZIP with `Kirby-Bridge.exe`, or the Linux setup tarball with `Play-Kirby.sh`. The bridge opens setup on first use, then plays the selected verified build. | Catalog acceptance, asset choice, first setup, update, and relaunch in the actual Quiver app. |
| RecompLauncher | Submit the repository at https://recomplauncher.com/submit?platform=pc. Ship `recomplauncher.json` inside each archive/AppImage, not only in the source repository. | Profile runs the bridge's noninteractive `prepare` step, then plays without opening Kirby's launcher. Settings use stable TOML paths. | Developer tools profile check, disc/folder setup, update, cancellation, and play on Windows/Linux. |
| Decomp Buddy | Already listed at https://decompbuddy.com/games/kirby-s-epic-yarn-wii. Its catalog and published plans are maintained upstream. | Refresh the existing plan for Windows and the current separate installer. Prefer `Kirby-Bridge.exe` on Windows or `Play-Kirby.sh` from the Linux tarball for persistent library launch. | Upstream plan refresh; real desktop app install/update/play on both platforms. |

Kirby supports **Windows x86-64 and Linux x86-64**. Launcher support for macOS,
Android or ARM does not make Kirby compatible with those targets. Linux packages
require glibc 2.39 or newer, working Vulkan drivers, a desktop session, several GiB
of free space and internet for the initial tools download. Do not advertise a
native macOS/Android build or validated Flatpak sandbox support.

## Why a JSON alone is insufficient

Kirby's releases contain authored sources and an installer, not a precompiled game.
The player's own USA Wii **RK5E01 revision 0** copy is extracted, checked, translated,
compiled and verified locally. `sys/main.dol` must have SHA-1
`f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`. This is **not** an ISO/RVZ/WBFS checksum:
do not put it in a launcher's whole-disc hash field.

Quiver 4 reads its catalog's `InstallSettings` (`folderName`, `filesToAdd`,
`preferredExecutables`, `setupFiles`, `saves`); no developer-side `quiver.json`
reader was found in the inspected source. Quiver can also add a repository or an
existing installation locally, but doing so does not submit it to the public catalog.

RecompLauncher explicitly supports a console preparation program. The bridge reuses
Kirby's existing verified worker instead of implementing another build pipeline.
The initial profile targets RecompLauncher **0.90.0-beta.65 or newer**, the SDK inspected here.
It prints `progress <fraction> <stage>` lines, where each of setup's five phases is an
equal fifth that never moves back, and returns nonzero on failure. The release
packagers stamp `prepare.version` with the exact source revision so updates rebuild
from the player's copy. The source template alone does not enable released builds.

Decomp Buddy reads project docs and publishes install plans. Its installed
`decomp-buddy.json` is library state created by that app, not a manifest Kirby
should ship or overwrite. Its current web entry advertises only Linux and retains
instructions for the earlier integrated launcher; request a refresh, not a duplicate.
Do not mistake its extracted quote about a missing FPS selector for a confirmed FPS feature.

## Bridge commands and package contents

Windows ZIP includes `Kirby-Bridge.exe` and its separate `bridge-python/` runtime.
Linux tarball includes `Play-Kirby.sh` and `tools/bridge/Kirby-Bridge`. The AppImage
routes `--prepare`, `--play`, and `--external-launcher` to that same bridge.
Opening the AppImage normally still opens the ordinary installer.

```text
Kirby-Bridge.exe --prepare --game "D:\My Games\Kirby.wbfs" --destination "D:\Recomps\Kirby\Game"
Kirby-Bridge.exe --play --destination "D:\Recomps\Kirby\Game"
./Kirby-Setup-linux-x86_64.AppImage --prepare --game "/games/Kirby.rvz" --destination "/games/Kirby/Game"
./Kirby-Setup-linux-x86_64.AppImage --play --destination "/games/Kirby/Game"
```

For a complete extracted dump, pass its DATA folder instead of an image. Source
developers can use `python3 tools/launcher_bridge.py` with the same options.
No host Python, Qt, compiler or container installation is needed by packaged users.
The existing worker still rejects unsupported revisions before compiling them.

The bridge's own errors have their own exit codes, apart from the game's (0 to 2):
**20** when there is no verified build to play yet, and **21** for anything else,
such as an incomplete release folder or a failed setup. The reason goes to stderr.

Run `Kirby-Bridge.exe` or `Play-Kirby.sh` without options for first-use setup and
subsequent play. Set the library's executable to that entry, not the setup GUI or
worker. In Quiver, choose the **Linux tarball** for this flow: the AppImage's normal
entry intentionally stays an installer. Do not filter Windows onto the Linux build.
Catalog preferred programs are `Kirby-Bridge.exe` (Windows) and `Play-Kirby.sh` (Linux).

Managed data relative to the release folder:

| Contents | Path |
| --- | --- |
| Current verified build selection | `Game/kirby-managed.json` |
| Settings | `Game/local/user/settings.toml` |
| Wii save data | `Game/local/user/nand/title/00010000/524b3545/data/` |
| Game log | `Game/logs/game.log`; the four runs before it as `game.1.log` (newest) to `game.4.log` |
| Authored source/build workspace per revision | `Game/.kirby/<source-revision-prefix>/` |

The runtime starts with `Game` as its working directory, keeping saves and settings
stable across compiled revisions. Existing standalone installations are not imported
automatically: back up and transfer them deliberately. On Windows, save transfers
must preserve NTFS named streams. Automatic backup/cloud-sync compatibility is
**not certified**, and the profile deliberately omits top-level `saves` registration.
Do not copy the entire managed folder into cloud save storage.

RecompLauncher Big Picture requests fullscreen for that session. `--windowed` can
override it; saved fullscreen is otherwise controlled by `video.fullscreen`.
Alt+Enter is Aurora's existing session toggle. The launcher exposes **Display >
Start in fullscreen**; fullscreen does not raise internal rendering resolution.

## Before closing the integration issues

- [ ] Package a new Windows ZIP and Linux AppImage/tarball, including the frozen bridge and profile.
- [ ] Check the embedded and adjacent profile in RecompLauncher's Developer tools.
- [ ] Install from both an owned supported image and a complete DATA folder.
- [ ] Verify real game startup, sound, input and fullscreen on Windows and Linux.
- [ ] Cancel a build and confirm failure never enables Play.
- [ ] Update from a previous managed revision and confirm the same save loads.
- [ ] Submit/refresh the upstream catalog entries; link the acceptance results in the issues.
- [ ] Verify each launcher's library Play and Update behavior before marking it complete.

## Primary sources inspected

- [RecompLauncher developer guide](https://github.com/elripalda/RecompLauncher/blob/baecaf4/docs/DEVELOPERS.md),
  [profile schema](https://github.com/elripalda/RecompLauncher/blob/baecaf4/docs/recomplauncher.schema.json)
  and [copy validation](https://github.com/elripalda/RecompLauncher/blob/baecaf4/src-tauri/src/profile.rs).
- [Quiver 4 README](https://github.com/tgeorgiadis/quiver-launcher/blob/main/README.md),
  [catalog metadata types](https://github.com/tgeorgiadis/quiver-launcher/blob/main/packages/api/src/index.ts)
  and [executable detection](https://github.com/tgeorgiadis/quiver-launcher/blob/main/apps/launcher/src-tauri/src/install.rs).
- [Decomp Buddy README](https://github.com/TanookiStudios/decomp-buddy/blob/main/README.md),
  [plan format](https://github.com/TanookiStudios/decomp-buddy/blob/main/src/plan.js)
  and [Kirby's existing catalog entry](https://decompbuddy.com/games/kirby-s-epic-yarn-wii).
- [Pinned Aurora fullscreen configuration](https://github.com/patchzyy/aurora-wiicompiled/blob/33067e34a17ca680eb961d9e0f3d48ddb22a7d7f/include/aurora/aurora.h)
  and [Alt+Enter handler](https://github.com/patchzyy/aurora-wiicompiled/blob/33067e34a17ca680eb961d9e0f3d48ddb22a7d7f/lib/window.cpp).
