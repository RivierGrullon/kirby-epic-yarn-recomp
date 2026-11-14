# Settings

The launcher and native game share `local/user/settings.toml`. The launcher saves
changes when you select **Save settings**, select **Play**, or close its window.
Changes apply on the next game launch and do not require recompilation.

Find VSync and experimental performance mode under **Display**, master volume
under **Audio**, and the extracted game-folder selection under **Install**.
**Controls** lists connected gamepads and default bindings. **Saves** opens the
existing save folder; **Diagnostics** provides logs, settings recovery and paths.
The save action remains available on every page. Keyboard and SDL gamepad
navigation use Dear ImGui's standard controls.

Only implemented Kirby options are written:

```toml
[kirby]
schema_version = 1
gpu_resident_copies = false

[paths]
dvd_root = "/path/to/extracted/DATA"

[audio]
volume = 1.0

[video]
vsync = true
```

`audio.volume` is the master gain from 0.0 to 1.0. The launcher displays it as a
percentage. `gpu_resident_copies` controls **Experimental performance mode**; its
compatibility and frame pacing remain under investigation. Separate stable 30/60
FPS modes, display scaling, individual audio groups and remapping remain pending.
Unimplemented upstream settings are not presented as supported Kirby options.

Relative `dvd_root` paths resolve against the TOML file's directory. The launcher
writes an absolute path. A missing settings file uses defaults without creating a
file during direct game launch or inspection. Saves keep their existing location;
changing a DATA folder does not move or replace them.

## Existing launcher preferences

On first launch, if the TOML file is absent, the launcher migrates
`local/user/launcher.cfg`: DATA folder, master volume, VSync and the optional fourth
line for experimental performance mode. Relative legacy paths retain their old
working-directory meaning. The legacy file remains unchanged. Once the TOML file
exists, it takes precedence; editing `launcher.cfg` has no effect.

## Inspection and command-line overrides

From the repository root:

```bash
build/native/kirby-launcher --print-settings local/user/settings.toml
build/native/WiiCompiled --print-settings local/user/settings.toml
build/native/WiiCompiled --video --config local/user/settings.toml
```

Inspection prints the effective settings without booting the game or modifying a
file. Both programs use the same parser. The game automatically reads
`local/user/settings.toml` for `--video` and `--video-hidden`; other diagnostic modes
only load settings when `--config` is supplied.

Explicit game options override saved values: `--game-data DATA`, `--volume 0..100`,
`--vsync`/`--no-vsync`, and `--gpu-resident-copies`/`--ram-copies`. These overrides
do not change the file. For example:

```bash
build/native/WiiCompiled --video --config local/user/settings.toml --volume 50 --no-vsync
```

## Errors and recovery

Malformed TOML, invalid supported values, and missing or unsupported schema
versions produce an error. The original file is preserved. The launcher disables
Play and saving. Open **Diagnostics** for **Open Settings Folder** and **Reload
settings**. If there are unsaved edits, the latter is labeled **Discard changes
and reload**.
Correct the file and reload it, or move it aside to retain a backup before
restarting. To restore defaults rather than migrate old preferences again, move
both settings files aside. Never move or delete the NAND/save directory for this.

Updates preserve unrelated TOML keys and comments. Saving writes and flushes a
private temporary file beside the destination, then renames it into place. A
failed write leaves the previous file intact. This does not provide concurrent
editing or power-loss recovery guarantees.

The adapter in `runtime/src/app/settings.cpp` reuses the pinned WiiCompiled
`RuntimeConfigFile` TOML parsing, update and string-format helpers. It does not call
that project's default-file routines or load its global Mario Kart configuration.
The shared library is linked into both programs; upstream GPL notices remain in
the dependency checkout.
