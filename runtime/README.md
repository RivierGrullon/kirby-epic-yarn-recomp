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

Only `native-src/` is indexed as native game overrides by WiiCompiled. Moving a
replacement into that tree requires the same DOL/address/ABI audit as adding a new
replacement. Original translated game functions belong in ignored `generated/`, not
in the authored runtime source directories.

Build integration is in `../cmake/`; reversible changes to the pinned WiiCompiled
snapshot are in `../patches/wiicompiled/`; Aurora adaptations are in
`../patches/aurora/` and apply to a separate local snapshot. Four compatibility headers retain their old
flat names only inside the ignored upstream snapshot, where the existing patches
include them. Authored code uses the public header paths above. Upstream source and
license files remain in ignored `vendor/`, with attribution in `../THIRD_PARTY.md`.

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
existing axis layout and leaving saved mappings to Aurora's normal loader.
It uses a virtual SDL device and its own input configuration; physical input and the
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

The native desktop frontend is in `../launcher/main.cpp`, built as `kirby-launcher`.
It shares existing SDL3/ImGui libraries but runs the game as a separate process.
Folder/revision checks precede launch, and `--volume` sets backend gain before startup.
