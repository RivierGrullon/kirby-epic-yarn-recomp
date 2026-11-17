# Compact AppImage Implementation Plan

**Goal:** Ship a small Linux AppImage that installs and reuses verified build tools.
**Architecture:** Keep the frozen worker in the image, with a pinned HTTPS archive
descriptor. Install the reduced tools atomically in a shared content-addressed cache.
**Tech stack:** Python stdlib, PRoot, tar/xz, existing AppImage runtime and SquashFS.
**Spec:** `docs/superpowers/specs/2026-11-17-compact-appimage-design.md`

## Constraints and review focus

- Preserve embedded bundles, developer containers, Windows behavior and saved games.
- Download only for disc inspection or installation; keep launch and DATA inspection offline.
- Fail closed on incorrect hashes, unsafe links, archive paths and translator revisions.
- Cancellation and simultaneous versions cannot expose partial tools.
- Preserve corresponding source and licenses; no game data in artifacts.

### Task 1: Shared verified tools download

Files: new `tools/installer/toolchain_download.py`, `tests/tooling/test_toolchain_download.py`;
modify `portable.py`, `install.py`, `test_appimage_setup.py`.
Interface: `ensure_toolchain(root, job)`; `cached_toolchain(runtime)` for `PortableTools.find`.

- [ ] Write failing real archive/download/cache tests (reuse, corruption, cancellation,
      unsafe paths/links and size limits) and worker-routing tests.
- [ ] Implement pinned descriptor validation, bounded download, safe extraction,
      cancellable cache lock, atomic selection and integration before disc tools.
- [ ] Run focused tests, then tooling suite.

### Task 2: Reduced toolchain and compact package

Files: new `tools/package_toolchain.py`; modify `package_portable.py`, `build_appimage.py`,
`kirby.py`, `portable.py`, package tests and packaging documentation.
Interfaces: descriptor `{version, platform, url, sha256, size, unpacked_size, image_id}`;
archive contains `rootfs/`, `proot`, `lib/`, `toolchain.json` and prebuilt translator.

- [ ] Write failing tests for tools exclusion, source retention, prebuilt translator
      selection and compact package validation.
- [ ] Build the translator at packaging time, prune SDK/Git/packaging tools, archive
      selected files and generate descriptor. Keep worker Python in the AppImage.
- [ ] Run focused tests and full suite; update package and cache documentation.

### Task 3: Real artifact verification

- [ ] Freeze current worker; produce reduced tool archive and compact AppImage.
- [ ] Measure artifacts; exercise first-download/reuse with actual payload, probes of
      compiler, dotnet runtime, translator, DolphinTool and launcher self-test.
- [ ] Review diff, run final checks, commit branch; report artifact locations and
      publication status explicitly.
