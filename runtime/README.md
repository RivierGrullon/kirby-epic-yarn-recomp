# Runtime layout

Public headers use `#include "kirby/<module>/<name>.h"`. Source filenames describe
their function; the `Kirby` namespace and verified revision profile identify the target.

| Directory | Responsibility |
| --- | --- |
| `include/kirby/` | Public interfaces grouped by module |
| `src/app/` | Native entry, host exit handling, startup diagnostics and integration probes |
| `src/boot/` | Retail memory setup and clocks |
| `src/devices/` | Interrupts, VI/SI, IPL/EXI, DSP task boot and audio DMA |
| `src/services/` | IOS requests, extracted disc access, NAND and ES metadata |
| `src/graphics/` | GX FIFO transport, packet decoding, Aurora rendering and XFB copies |
| `src/threading/` | Cooperative native stacks, CPU scope and floating-point state preservation |
| `src/input/` | Host controller defaults configured before PAD initialization |
| `native-src/devices/` | Verified DSP task-loader boundary |
| `native-src/services/` | Audited IOS/interrupt callback boundaries |
| `native-src/threading/` | Audited SDK scheduler and context-return boundaries |
| `native-src/input/` | Audited WPAD bridge to native input |
| `native-src/graphics/` | Audited SDK graphics upload boundaries |

Only `native-src/` is indexed as native game overrides by WiiCompiled. Moving a
replacement into that tree requires the same DOL/address/ABI audit as adding a new
replacement. Original translated game functions belong in ignored `generated/`, not
in the authored runtime source directories.

`native-src/graphics/matrix.cpp` registers only RK5E01's verified
`GXLoadPosMtxImm` entry. `src/graphics/pos_matrix.cpp` uploads through the existing
32-byte FIFO transport, retaining the original header/load order and paired-single
conversion rules. Nondefault quantization and noncontiguous fast memory ranges
use the locally translated function. Preparation verifies the function's symbol,
extent and body digest; the native probe compares FIFO bytes, callbacks, memory
and CPU state against that original function. It does not replace Aurora's matrix
rendering or change game timing. The native effect boundary remains conservative.
After adding or changing indexed overrides, rerun the complete translation before
compiling so generated callers and shard exclusions use the current native index.

Build integration is in `../cmake/`; reversible changes to the pinned WiiCompiled
snapshot are in `../patches/wiicompiled/`; Aurora adaptations are in
`../patches/aurora/` and apply to a separate local snapshot. Four compatibility headers retain their old
flat names only inside the ignored upstream snapshot, where the existing patches
include them. Authored code uses the public header paths above. Upstream source and
license files remain in ignored `vendor/`, with attribution in `../THIRD_PARTY.md`.

`include/kirby/app/build_info.h` and `src/app/build_info.cpp` expose compiled build
records shared by the launcher and runtime. `../tools/build_info.py` generates the
private header through CMake; it is excluded from the game PCH and translated
shards. Both programs' `--build-info` handlers return before settings or device
initialization. `../launcher/build_probe.{h,cpp}` owns the asynchronous runtime
query, its timeout, bounded output and child cleanup. The Diagnostics page shows
the queried record separately from the launcher's record. See the
[build identity guide](../docs/building.md#identify-the-executable-being-used).

The tests mirror these boundaries: C++ unit tests under `../tests/unit/`, executable
checks under `../tests/integration/`, and synthetic DOL/tooling checks under
`../tests/tooling/`. Run Python discovery from the repository root with
`python3 -m unittest discover -s tests -v`; set `KIRBY_NATIVE_TEST` to enable integration
checks against a local executable.

Earlier bring-up records retain the paths used when those results were captured.
The old `runtime/kirby_<name>.cpp` files now live under the matching `src/<module>/`
directory; headers moved to `include/kirby/<module>/`. Native override files lost the
redundant `kirby_` filename prefix and moved into their module under `native-src/`.
No game data or generated game source was moved into these directories.

`src/graphics/gx_draw.cpp` validates direct/indexed draws, sampled RAM textures and palette
TMEM uploads before passing commands to Aurora. Indexed arrays upload only their
referenced extent; indexed NBT normals remain an explicit boundary. `gx_xfb.cpp` queues Dawn polling on the render worker to serialize
it with submissions. Synthetic colors verify readback and VI selection; they are not
a game screenshot.

`src/devices/ax.cpp` selects the audited AXWii microcode layout in the existing
WiiCompiled mixer. `audio_dma.cpp` models the 32 kHz DMA latch and AID interrupts.
`--probe-ax-task` exercises the original loader, IRQ callback, mailbox replies and
96 output samples. `--probe-alarms` checks the original SDK alarm queue, deadlines,
cancellation and three resumed `OSSleepTicks` calls.

`OSRestoreInterrupts` delivers pending hardware IRQs at its audited native boundary,
including immediate re-entry for queued DSP replies. IRQ-frame state travels with
each native thread stack; preempted contexts retain the exception-entry register
save instead of overwriting it with handler scratch registers. Unmatched preempted
contexts and arbitrary replacements still stop explicitly. The AX probe checks
masked deferral, two queued replies and the restored SDK context outside idle.

With the native IRQ-state callback installed and AID enabled, `DspBoot::Poll`
advances audio DMA only to the next buffer interrupt. It lets the original guest
refill handler return and queued DSP replies complete before consuming another
overdue buffer. Masked AID and standalone device probes retain continuous raw DMA.
The clock, 32 kHz sample rate and bounded catch-up policy are unchanged. This is
native event scheduling, not a claim of cycle-accurate Wii hardware emulation.

`gx_xfb.cpp` explicitly preserves EFB attachments when resuming after GPU readback
and VI presentation. Stage 20 verified the real wrist-strap screen in the native
window; see `../docs/bringup.md`.

EFB texture copies share the serialized GPU readback path with XFB copies. Native
texture tiles are written to guest RAM with tile-row stride; stale GPU copy cache
entries are evicted so CPU edits remain visible. `xfb.cpp` decodes both copy layouts.
NAND creation persists local permission/attribute bytes in `user.kirby.isfs` xattrs
and boot initializes `/tmp`. Attribute queries read the persistent records; file
commits use descriptor-relative rename with IOS basename/open-file constraints.
Initial save creation, main-menu navigation, Patch Castle completion and Fountain
Gardens movement/jumping are verified in the native binary using virtual PAD input.
Physical controller validation remains pending. An independent native restart also
loaded the updated File 1 into Quilty Square with the unlocked house. The
`tests/integration/controller_probe.cpp` diagnostic covers SDL gamepad events through
Aurora and the guest WPAD serializer, including release and unplug while held.
The same checks cover standard, PS5, Xbox 360 and Xbox One presets. Kirby sets
positional Xbox face-button defaults before PAD initialization, preserving the
existing axis layout. Video launches apply the shared settings after PAD
initialization; isolated SDK probes retain their existing defaults.

`patches/aurora/controller-binding-overrides.patch` adds a binding overload that
replaces selected physical controls rather than appending alternatives. It keeps
Aurora's per-port routing, capture, virtual producers and device source resolution.
An explicit shared-keyboard option enables keyboard actions alongside a controller
without changing the existing exclusive-keyboard mode. The SDL probe checks
jump/yarn and plus/minus swaps, simultaneous keyboard/controller input, blocking,
reset and reconnect with a new SDL instance across four virtual device presets.
`include/kirby/input/bindings.h` defines the common action/default model;
`src/input/bindings.cpp` reuses `ControllerNames` positional tokens and SDL physical
scancode names. The shared TOML schema validates conflicts and required actions.
`src/input/controller_mapping.cpp` transforms the model into Aurora bindings,
replacing only the selected physical controls while preserving routing and the
existing sideways guest WPAD serializer. Keyboard bindings share player 1's port.
The launcher capture model lives in `../launcher/binding_capture.{h,cpp}`; its
Controls page offers capture, conflict/swap, clear and reset. See the
[settings guide](../docs/settings.md#control-bindings) for scope and defaults.
`--probe-configured-controls --config TOML` checks saved profiles through the guest
WPAD reports across four virtual device presets, including obsolete binding
removal, keyboard/controller sharing and release. These probes use virtual SDL
devices and their own input configuration; physical input and the
window's focus/capture behavior still require verification.
Its `--monitor-controller` mode reads physical devices for 30 seconds without a
virtual device or game window and logs changes at the SDL/PAD/guest boundaries.
Use the isolated command in the project README to preserve existing preferences.
`GxRenderer::OpenWindow(startHidden)` forwards the explicit diagnostic visibility
setting to Aurora. `--video-hidden` exercises the Vulkan/GX/VI path without mapping
a visible window. The GPU test's matching mode advances 16 additional VI frames.
`include/kirby/app/events.h` checks Aurora's event list after its noexcept update
returns, propagating exit requests through native continuations to `main`.
Main's destruction order releases the owned audio stream and guest continuations
before the renderer shuts down Aurora/SDL. The optional native video integration
test verifies SIGTERM exit after controller/AX startup and the first GX draw.

Indexed attribute snapshots let `gx_draw.cpp` reuse an unchanged upload within the
current frame while checking RAM on every draw. Redundant CP base writes preserve
the cached range; changed data and frame retirement still invalidate it.
The AX memory adaptation reads actual configured MEM2 bounds, avoiding a null
sample window when the retail 64 MiB map is smaller than the upstream NDEV limit.
Its integration probe includes a synthetic MEM2 PCM16 voice, not only bus constants.

The native desktop frontend is built as `kirby-launcher`. Its process and folder
dialog ownership remain in `../launcher/main.cpp`; `../launcher/state.h` contains
the view state and actions. `../launcher/pages/` holds the layout and separate
Play, Install, Display, Audio, Controls, Saves and Diagnostics views. Pages edit
shared settings or return actions; they do not own subprocesses or file writes.
It shares existing SDL3/ImGui libraries but runs the game as a separate process.
Folder/revision checks precede launch, which passes the shared TOML path using
`--config`. `src/app/settings.cpp` and `include/kirby/settings.h` form the common
configuration adapter; see `../docs/settings.md` for its schema and migration.
The launcher's hidden `--self-test` renders every page at desktop and compact
sizes without dispatching file or process actions.

`../launcher/input_focus.cpp` clears queued and held ImGui input when the launcher
does not have keyboard focus or is minimized. This is required because the SDL
backend polls global controller state. Actions are also checked against current
window focus before dispatch. The ImGui input regression covers initial absence
of focus, repeated background polling, focus loss/regain, interrupted clicks and
fresh foreground input; it does not certify physical controller mappings.
