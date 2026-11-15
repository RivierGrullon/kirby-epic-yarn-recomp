# First-time Setup Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Resolve issue #1's first-installation confusion with a usable setup command.

**Architecture:** A Python CLI performs host checks and prepares the toolchain,
then calls the existing installer. An optional launcher-preparation callback runs
before game selection so a launcher failure cannot select an inaccessible game.

**Tech Stack:** Python standard library, Bash, existing Podman/CMake installer.

**Spec:** `docs/superpowers/specs/2026-11-14-first-time-setup-design.md`

## Global Constraints

Linux x86-64 and native Windows x86-64, Python 3.11+, RK5E01 revision 0. No sudo or automatic host package
installation. Preserve unrelated local changes, preferences, saves and DATA.

## Review Focus

- Missing tools must fail before bootstrap, not midway through a build.
- Image paths with spaces and shell syntax must remain literal arguments.
- Existing DATA folders must not require extraction tools or be overwritten.
- Launcher verification failure must preserve the old launcher/game selection.
- Cancellation must stop managed work and report no false completion.

## Tasks

- [x] Add failing CLI/preflight/launcher tests and an installer callback failure test.
- [x] Implement `tools/setup.py`, actionable missing-Podman errors in
  `tools/container.sh`, and optional `prepare_launcher(build, identity, image)`
  in `execute_install` before publication/selection.
- [x] Run focused tests; resolve failures.
- [x] Rewrite primary README/building instructions and move optional inspection
  below the manual build/run instructions.
- [x] Run tooling suite, CLI smoke checks, shell syntax and diff checks; review
  the final change and report actual validation limits.

## Execution notes

Implementation authorized by the user's selection of the single-command option.
Work stays in the shared checkout and excludes pre-existing graphics edits.

## Expanded native Windows work

- [x] Share Docker/Podman command construction and ownership cleanup, including
  backend identity in recovery receipts.
- [x] Port authored runtime file services/settings/frame logs (windows_runtime).
- [x] Port launcher processes, manifests and UTF-8 paths (windows_launcher).
- [x] Port host worker locking/process trees/paths/PE verification (windows_worker).
- [x] Pin LLVM-MinGW toolchain and cross-build Windows products, bundle runtime DLLs.
- [x] Run Linux tests, Windows cross-compiles/Wine tests and add native Windows CI.

Execution is split by independent files following dispatching-parallel-agents.
Pre-existing graphics edits remain excluded from this work.

## Verification record

- Python discovery: 213 tests, OK, 60 opt-in/platform skips; an additional
  opt-in COFF assembly test passed inside the Windows toolchain container.
- Linux native Kirby CTest selection: 32/32 passed. The upstream `mkw_*` test
  executables are separate targets and were not built by `kirby_tests`; an
  initial unrestricted CTest run therefore reported ten of those as Not Run.
- Native Linux launcher integration: 5/5 passed against the rebuilt launcher.
- Windows Python under Wine: 27 tests, OK, two platform/capability skips.
- Windows executable Unicode argument test passed under Wine.
- Windows launcher compiled/linked; actual installation via the production
  launcher-publishing code, UI self-test, real DATA verification and Python
  worker/source protocol all passed under Wine. Container compilation was
  mocked only for that installation check because products were already built.
- Windows native game and launcher cross-compilation passed. The actual game
  passed --build-info, --verify-bootstrap and --probe-boot-memory with the local
  DATA folder under Wine. All embedded data sections loaded at the expected
  addresses; MEM1/MEM2 aliases and the IPC buffer probe passed.
- NTFS EA and junction behavior requires native Windows; workflow added, not run
  remotely. Wine fails the NTFS EA tests and cannot create the junction fixture.

## Additional fixes proved by Windows execution

- Valid installed artifacts originally overflowed the default Windows stack:
  the reader's 1 MiB hash buffer now lives on the heap. Three real Python/C++
  manifest integration tests pass, covering Unicode, DLLs and corruption.
- Windows settings execution exposed an ANSI conversion in the TOML diagnostic
  filename and CRLF on legacy path lines. UTF-8 conversion and CRLF handling
  fixed these; 37 native settings checks and the SDL build-probe checks passed
  under Wine.
- Cross-compiling Linux-generated data required COFF section directives. An
  owned CMake build copy preserves payload bytes and source outputs. A synthetic
  cross-assembler test verifies the read-only COFF section and payload refresh.
- Native registry checks compared string pointers; Windows boot now verifies
  name contents and passes all required native registration checks.
- Kirby does not expose external-media music ducking. Its Windows build
  disables that optional upstream monitor instead of requiring C++/WinRT SDK
  headers. Master volume remains implemented normally.
- The pinned Dawn binary imports Microsoft's v14 C++ runtime. Windows preflight
  detects missing runtime DLLs and links to Microsoft's x64 redistributable.

No gameplay or NTFS extended-attribute compatibility claim is made from Wine.
The native Windows workflow is prepared for those OS-specific checks but has
not been run remotely. Changes remain in the local checkout.
