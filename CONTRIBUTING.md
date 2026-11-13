# Contributing

Contributions should make the native game more correct, reproducible or usable.
Read the [build guide](docs/building.md), [runtime layout](runtime/README.md) and
[current limitations](README.md#known-limitations) before changing runtime behavior.

## Bug reports

Include the commit, Linux distribution, CPU/GPU, graphics driver, game revision,
scene and steps to reproduce. State whether Experimental performance mode and
VSync were enabled. For controls, include the controller model, connection type,
button pressed and observed result. For audio, distinguish silence, crackling,
wrong tempo and synchronization drift.

Attach a relevant log excerpt or a short screenshot/video of the issue. Review
logs for personal paths before sharing. Do not upload disc images, extracted game
files, translated game source, data initializers, save files or shader caches.

Performance reports should compare the same scene, camera and settings with a
warm shader cache. Report hardware, visible versus hidden window, frame intervals
and actual game/audio behavior. The title-bar FPS counter alone is insufficient.

## Changes

- Keep code, comments, documentation and commit messages in English.
- Keep changes focused and explain the behavior before and after the change.
- Verify RK5E01 addresses and layouts against the DOL and community symbols.
  Another game's hook or memory constant is not evidence for Kirby.
- Keep upstream checkouts intact. Use `patches/` for shared-runtime changes and
  retain licenses and attribution when reusing source.
- Add focused regression coverage for rendering, timing, memory visibility or
  input behavior that changes. Record limitations of the verification.

## Tests

Without a game or native build:

```bash
python3 -m unittest discover -s tests -v
```

Native integration tests are skipped unless their executable paths are provided.
CI runs the synthetic tooling tests; it does not build or execute the game.

With a local build and host graphics session:

```bash
bash tools/container.sh cmake --build build/native --target kirby_gx_renderer_test --parallel 2
LD_LIBRARY_PATH="$PWD/local/host-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" build/native/kirby_gx_renderer_test --gpu-xfb-hidden
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" \
KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" \
KIRBY_LAUNCHER_TEST="$PWD/build/native/kirby-launcher" \
LD_LIBRARY_PATH="$PWD/local/host-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
python3 -m unittest discover -s tests -v
```

Run C++ unit targets relevant to your change with CTest. Check that the requested
targets were built; a stale executable is not validation of new source.

## Pull requests

Describe the concrete issue, resulting behavior, tests performed and remaining
limitations. Include before/after timings for performance changes and matching
scene captures for graphics changes. Do not claim full-game compatibility from
startup tests or physical-controller support from virtual input.

Keep game data, generated translations, binaries and local diagnostics out of
commits. Documentation screenshots should come from an actual native run, with
their origin and test conditions stated. Preserve existing save data during tests.
