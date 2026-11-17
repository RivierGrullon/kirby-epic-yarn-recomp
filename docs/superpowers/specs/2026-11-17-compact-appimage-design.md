# Compact Linux AppImage

The user selected Longshot's distribution model: a small AppImage and a verified,
one-time tools download, reused across application versions. Keep the existing
own-disc extraction, translation, build, verification and launch experience.

The AppImage contains the frozen Python worker, launcher, authored source snapshot,
licenses and a pinned download descriptor. A separate `.tar.xz` contains the PRoot
build environment. Its SHA-256 addresses a shared directory under
`$XDG_DATA_HOME/kirby-native/toolchains/`; preparation is atomic and locked across
versions. Download only when inspecting a disc image or installing a game, never
when simply opening the launcher or playing an installed game. A DATA-folder
inspection stays offline.

Verify HTTPS, compressed size, SHA-256, bounded extraction, internal link targets,
tool manifests and translator revision before making tools available. Corruption,
cancellation or a network failure must leave no selected partial toolchain and
must preserve installations and saves. Progress uses existing `prepare` events.
Keep support for the previous embedded toolchain format and developer containers.

Build the pinned translator before packaging; distribute its DLLs with the .NET
runtime, excluding the SDK. Prune packaging-only PyInstaller files, PowerShell,
caches and Git metadata from the tools payload. Preserve corresponding third-party
sources and notices. The packaging command produces both the download archive and
the descriptor; it does not publish or replace a release automatically.

Alternatives: pruning the embedded environment still repeats large downloads on
every release; a complete native-tool redesign adds unnecessary installation risk.
The selected approach separates and reduces the existing tested environment first.
Keep Python stdlib and existing PRoot commands; no host dependencies are added.

The user's additional requirement is visible progress throughout setup. Show the
current stage and elapsed time; downloaded/unpacked bytes and Ninja completed tasks
drive determinate bars, while stages with unknown totals use animated activity.
Do not estimate an overall install percentage or label build task counts as bytes.

Validation: synthetic downloads and hostile archives; cache reuse across source
versions; cancelled/corrupt transfers; worker integration; tooling suite; a real
AppImage size measurement and isolated worker/toolchain smoke checks. Full gameplay
validation remains separate from packaging checks.
