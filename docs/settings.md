# Settings

The launcher and native game share `local/user/settings.toml`. The launcher saves
changes when you select **Save settings**, select **Play**, or close its window.
Changes apply on the next game launch and do not require recompilation.

Find VSync and experimental performance mode under **Display**, master volume
under **Audio**, and the extracted game-folder selection under **Install**.
Install can import an image/archive or verify a DATA folder in the background.
It changes the selected folder only after successful verification and worker exit;
cancelling or failing an import keeps the previous selection.
**Controls** lists connected gamepads and edits gamepad/keyboard bindings. **Saves** opens the
existing save folder; **Diagnostics** provides logs, settings recovery and paths.
The save action remains available on every page. Keyboard and SDL gamepad
navigation use Dear ImGui's standard controls.
The launcher accepts input only while its own window has focus. Gamepad polling
in the background cannot change its settings or activate buttons while you play
in another window. Losing focus also clears held input.

The game opens in a 960×540 window and presents at 16:9, matching the default Wii
widescreen setting. Resizing the window preserves that ratio with black bars as
needed. **Display > Internal resolution** selects a fixed 1x–6x and 8x rendering multiplier,
independent of window size. **Anti-aliasing** offers Off, FXAA, 4x MSAA, and 4x MSAA + FXAA. Both apply on
the next launch and work with experimental performance mode on or off.

The native EFB workspace is 640×528; 3x renders it at 1920×1584 and 6x at
3840×3168; 8x renders at 5120×4224, with the game's visible area presented at 16:9. This is higher-resolution
geometry rendering, as in an emulator's internal resolution setting. Original
texture assets retain their original detail. Increasing resolution also smooths
edges when the result is reduced to a smaller window; MSAA adds polygon-edge
coverage samples. FXAA filters edges after output resizing and can soften fine detail;
the combined mode uses both methods. Higher settings cost GPU time and memory. Start with 2x or 3x
and 4x MSAA, then adjust to your hardware. Defaults remain 1x with MSAA off.

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
resolution_scale = 1
msaa_samples = 1
fxaa = false
```

`audio.volume` is the master gain from 0.0 to 1.0. The launcher displays it as a
percentage. `gpu_resident_copies` controls **Experimental performance mode**; its
compatibility and frame pacing remain under investigation. `resolution_scale`
accepts integers 1 through 6 or 8; `msaa_samples` accepts 1 (off) or 4. `fxaa` is a boolean, defaulting to false. Missing keys use
the defaults, so existing configuration files keep loading. Unsupported values
are reported without overwriting the file. Separate stable 30/60 FPS modes,
individual audio groups and per-device input profiles
remain pending.
Unimplemented upstream settings are not presented as supported Kirby options.

Relative `dvd_root` paths resolve against the TOML file's directory. The launcher
writes an absolute path. A missing settings file uses defaults without creating a
file during direct game launch or inspection. Saves keep their existing location;
changing a DATA folder does not move or replace them.

## Control bindings

Open **Controls**, select an action's gamepad or keyboard slot, then press the new
input. Gamepad capture accepts buttons and signed stick/trigger directions. Center
a stick before moving it; release the captured input before continuing. Initially
held inputs and keyboard repeats are ignored. Escape or **Cancel capture** cancels
without changing a pending binding. Keyboard capture has a **Use Escape** button
for assigning Escape itself. Losing focus, disconnecting the captured controller,
or waiting 15 seconds for an input cancels capture.

If an input already belongs to another action, the launcher leaves both bindings
unchanged and offers **Swap bindings** or cancellation. **Clear** removes one
binding; movement, jump, yarn, Plus and Minus must retain at least one gamepad or
keyboard binding. **Reset controls** restores default bindings and axis activation
levels. Use **Save settings** to
persist changes; they apply on the next game launch. Launcher navigation uses its
own standard controls and is unaffected by gameplay remapping.

**Stick activation** and **Trigger activation** choose how far an axis assigned
to an action must move before that action becomes pressed. Both default to 50%
and accept 5-95%. Lower values activate earlier; higher values require more
travel. The settings apply to all players' gamepad bindings and take effect on
the next game launch. D-pad buttons and keyboard bindings retain their existing
behavior. Continuous analog deadzones remain pending.

The optional `[controller]` values are `stick_activation` and
`trigger_activation`, expressed as fractions from `0.05` to `0.95`:

```toml
[controller]
a = "right_stick_right"
b = "right_trigger"
stick_activation = 0.25
trigger_activation = 0.75
```

In this example, moving the right stick past 25% to the right presses Jump;
pulling the right trigger past 75% presses Yarn. Configurations without these
keys keep the previous 50% threshold. Invalid values are reported while leaving
the settings file untouched. Virtual-device tests check partial travel on both
sticks and triggers through the original guest WPAD reports for standard, PS5,
Xbox 360 and Xbox One presets. Physical-device validation remains pending.

Gamepad mappings currently apply to every player, while keyboard input is shared
with player 1's controller. Per-device profiles, player assignment, movement-stick
deadzones and rumble options are not implemented here. Physical
controllers, reconnect behavior during gameplay and cooperative play still need
validation; automated virtual-device tests do not certify them.

The optional `[controller]` and `[keyboard]` tables use these keys and defaults:

| Key | Action | Gamepad token | Keyboard scancode name |
| --- | --- | --- | --- |
| `up` | Move up | `dpad_up` | `Up` |
| `down` | Move down | `dpad_down` | `Down` |
| `left` | Move left | `dpad_left` | `Left` |
| `right` | Move right | `dpad_right` | `Right` |
| `a` | Jump / Confirm (Wii 2) | `south` | `Space` |
| `b` | Yarn / Cancel (Wii 1) | `east` | `Left Ctrl` |
| `y` | Wii A | `north` | `X` |
| `x` | Wii B | `west` | `Z` |
| `start` | Pause / Plus | `start` | `Escape` |
| `back` | Map / Minus | `back` | `Tab` |
| `guide` | Home | `guide` | `F1` |

Gamepad tokens use the pinned runtime's positional `ControllerNames` vocabulary.
Keyboard values identify physical SDL scancodes, not typed characters; `disabled`
explicitly removes either kind. Old settings files without these tables use the
defaults. Unknown tokens, duplicate bindings within a device kind and required
actions with both inputs disabled produce a settings error. Saving preserves
unrelated keys and writes canonical binding names.

For example, bind jump to the right shoulder and physical J key:

```toml
[controller]
a = "right_shoulder"

[keyboard]
a = "J"
```

The configured-input diagnostic exercises these settings through the native
WPAD bridge using virtual SDL devices; it does not boot a playable level. Run it
from a disposable directory because Aurora persists its test port assignments:

```bash
project_root="$PWD"
probe_dir="$(mktemp -d)"
(
  cd "$probe_dir" &&
  "$project_root/build/native/WiiCompiled" --probe-configured-controls \
    --config "$project_root/local/user/settings.toml" \
    --game-data "$project_root/disc/DATA"
)
```

## Motion controls

The **Gyroscope & gravity / tilt** area in **Controls** contains all motion
configuration; no F1 settings overlay is added. F1 retains its existing Home
binding. Hold a conventional gamepad normally; its gravity vector is converted
to a sideways Wii Remote with the D-pad at the left.

- Enable **Use controller tilt** for gravity-based steering, with sensitivity and
  reversal. Devices without an accelerometer stay neutral.
- **Pointer source** selects mouse, gyroscope, right stick or disabled. Gyroscope
  is the default and falls back to the mouse on player 1 when sensors are absent.
  Gyro/stick sensitivity and X/Y inversion apply to their respective pointer;
  their deadzones are independent of movement bindings.
- Click the right stick to recenter the pointer in game. **Keyboard motion** adds
  Q/E tilt and R recenter for player 1. These fixed helper keys can be disabled if
  they overlap custom button bindings. The right-stick click may also activate a
  button action if explicitly assigned in the binding table.
- Select a **Preview controller** to inspect live sensor availability, tilt and
  pointer position. This selection does not reassign game players.
- Select **Calibrate gyro**, then hold still for two seconds. Movement restarts
  the measurement. Save settings after completion. One controller model's SDL
  GUID and bias are saved; calibration is applied only to matching GUIDs. A new
  calibration replaces the previous one. Identical models share the calibration.

Configuration applies to all controller ports and takes effect on the next game
launch. Motion is suppressed while input is blocked or captured. Reconnecting
resets the pointer/filter and reloads matching calibration. Launcher previews stop
on focus loss, leaving Controls or binding capture; interrupted calibration is
discarded. Game input never depends on the launcher preview remaining open.

Optional `[motion]` defaults (older files need no migration):

```toml
[motion]
tilt = true
invert_tilt = false
tilt_gain = 1.0          # 0.25 .. 3
pointer = "gyro"        # mouse, gyro, right_stick, disabled
sensitivity = 1.5       # 0.1 .. 5
invert_x = false
invert_y = false
deadzone = 0.008        # gyro rad/s, 0 .. 0.1
stick_deadzone = 0.2    # pointer stick only, 0 .. 0.9
keyboard = true
calibration_device = "" # populated by launcher calibration
bias_x = 0.0
bias_y = 0.0
bias_z = 0.0
```

SDL must expose the physical controller's sensors. Xbox-emulation software may
hide them. Virtual sensor tests verify conversion, calibration, disconnection
and guest WPAD acceleration/IR reports; they do not certify physical-controller
directions, drift or gameplay behavior.

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
