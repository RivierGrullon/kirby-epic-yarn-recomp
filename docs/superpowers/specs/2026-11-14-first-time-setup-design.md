# First-time setup

The user selected a single installation command plus corrected documentation to
address issue #1. The existing launcher can build games, but cannot bootstrap
itself. Optional diagnostics currently interrupt the first-build instructions.

## Approach

Add `python3 tools/setup.py SOURCE`, defaulting to `disc/DATA` when already
extracted. Reuse the existing locked, cancellable source inspection and verified
installation pipeline. Detect Docker or Podman (explicit backend selection also supported), and prepare the target-specific toolchain image when absent. Windows .exe output uses pinned LLVM-MinGW cross-compilation inside the Linux build container, then native host verification. Before selecting
the game, build/check the launcher and atomically place it at
`build/native/kirby-launcher`, where it expects the repository root.

Documentation-only fixes leave the first-build burden intact. A distributed
binary installer would require separate packaging and compatibility work. The
selected command approach reuses existing implementation and is locally testable.

## Constraints

- Linux x86-64 and native Windows x86-64, Python 3.11+, own RK5E01 revision 0 data.
- Report missing host prerequisites before downloading or building; never run sudo.
- Require Dolphin only for images and 7-Zip only for archives.
- Preserve saves, preferences and existing DATA. Select only verified games.
- Validate launcher provenance before replacing an existing launcher.
- Use existing setup locking, managed build containers and cancellation.
- Provide readable stages, log locations and a prerequisite-only `--check` mode.
- Update README and build guide with one primary route and optional diagnostics
  after the manual build/run route. Explain example paths and existing extraction.

The existing installer remains available to the launcher. Publishing a portable
release and automatically installing OS packages are outside this change.

## Scope correction from the user

The user explicitly selected native Windows .exe, not WSL2. This includes C++
file/process portability, Python worker portability, target-separated caches and
PE/DLL verification. Existing upstream WiiCompiled already supports LLVM-MinGW.
Docker and Podman are alternatives; neither brand is a project requirement.
The game runs natively on its host; containers are compilation tools only.

Native Windows save metadata uses Windows extended attributes; keep the checkout
and saves on NTFS. Preserve the existing Linux file containment tests. Add
Windows reparse-point, process-tree, pipe and file-lock tests. Cross-compile and
run Windows unit binaries under Wine here, and provide real Windows CI checks;
state clearly that Wine is not full native Windows gameplay verification.
