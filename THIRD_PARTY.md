# Third-party sources

| Project | Use | License location |
| --- | --- | --- |
| [Swiftshine/key](https://github.com/Swiftshine/key) | RK5E01 revision hash, symbols, section splits, startup source cross-check | `vendor/key/LICENSE` (CC0-1.0) |
| [patchzyy/Wiicompiled](https://github.com/patchzyy/Wiicompiled) | Translator, C++ runtime, manifest and shard formats | `vendor/wiicompiled/LICENSE` (GPL-3.0) |
| [WarioRecomp](https://github.com/santiagovalencia109/Wario-Land-Shake-It-Native-PC) | Linux build sequence, container approach and CMake flag reference | `vendor/wario/LICENSE` (GPL-3.0) |
| [NewSuperPaperBros](https://github.com/josiahg2502-glitch/NewSuperPaperBros) | Community symbol retargeting and additional REL input reference | Component licenses in `vendor/paper/wiicompiled/`; reference only |
| [aurora-wiicompiled](https://github.com/patchzyy/aurora-wiicompiled) | WiiCompiled's pinned graphics/input dependency | `vendor/wiicompiled/aurora-main/LICENSE` (MIT, copyright Luke Street) |
| [JSON for Modern C++](https://github.com/nlohmann/json/tree/55f93686c01528224f448c19128836e7df245f72) | Bounded setup protocol parsing and request serialization in the launcher | `vendor/json/LICENSE.MIT` (MIT, copyright Niels Lohmann) |
| [NSMBWiicompiled](https://github.com/ral1009/NSMBWiicompiled/tree/0c01832a26aa2559ffd6b6ca489decf22f05c306) | Native GX binding/copy coherence, presentation instrumentation and public documentation reference; no source imported | Upstream `LICENSE` (GPL-3.0); Aurora retains its MIT license |
| [Dolphin](https://github.com/dolphin-emu/dolphin/tree/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW) | Interrupt registers, EXI immediate transfers, absent-device responses, IPL/EUART framing, SRAM layout, VI/SI registers and DI configuration reference | GPL-2.0-or-later; source headers credit the Dolphin Emulator Project |

Exact source commits are recorded in `dependencies.json`. Sources are fetched locally
and retain their license files. `tools/build_native.py` and the toolchain package list
follow the build approach in WarioRecomp's `tools/build-wario.sh`; no Wario addresses,
hooks, memory constants or gameplay patches have been applied to Kirby.

WiiCompiled and Aurora have additional dependencies with their own licenses. Preserve
their notices if runtime sources or compiled dependencies are redistributed. The local
dependency fetch does not change their licensing.

No Nintendo game data or C++ generated from the game is part of this repository.
Kirby and Kirby's Epic Yarn are Nintendo trademarks. This is an independent project.
Curated documentation screenshots depict locally running gameplay; their original
game artwork is not covered by this repository's source-code license. Raw captures
and recordings remain local. See `docs/images/README.md` for screenshot provenance.

The interrupt model and tests are newly written here under GPL-3.0-only. Dolphin was
used as a behavior reference at revision `db2afcbdf8f5175a6b8414d9f5e21c978775f651`;
its source files are not linked into the native product. Review copies, retaining
their original copyright and SPDX notices, are under ignored `local/references/dolphin/`.

`patches/aurora/controller-binding-overrides.patch` extends Aurora's PAD binding
adapter with replacement controls and opt-in shared keyboard/controller input.
The changes in that patch are provided under Aurora's MIT license. Its original
copyright and license notices remain in the pinned source tree. Kirby's GPL
integration tests exercise the existing routed input and guest WPAD serializer;
no game-specific addresses or disc instructions are included in the patch.


The SRAM layout reference in Dolphin's `Sram.h` retains its embedded libogc notice,
crediting Michael Wiedenbauer and Dave Murphy; the review copy is kept intact locally.
The new storage interface uses synthetic console settings and computes its own checksum;
no console SRAM dump or Nintendo ROM bytes are included. RTC time is supplied by the
existing WiiCompiled `PPC_Mftb`/`PPC_Mftbu` backend and `timebase_contract.h`.

The native boot-memory profile also references Dolphin's `IOS/VersionInfo.cpp`,
`IOS/IOS.cpp`, `Boot/Boot_BS2Emu.cpp` and retail console identifiers in `Core.h` at
the same pinned revision. The selected IOS56 values are attributed to the Dolphin
Emulator Project (GPL-2.0-or-later). Original source notices remain in local review
copies. The Hollywood audio-clock register map is cross-checked against `WII_IPC.cpp`
and the SDK sequence in Swiftshine/key; no Dolphin CPU or DSP emulator is linked.

DSP boot behavior also references Dolphin's `HW/DSPHLE/DSPHLE.cpp`,
`HW/DSPHLE/UCodes/INIT.cpp`, `HW/DSPHLE/UCodes/ROM.cpp`, and
`HW/DSPHLE/MailHandler.cpp` at the pinned revision. Copyright notices credit the
Dolphin Emulator Project (GPL-2.0-or-later) and remain intact in local review copies.
The project implementation is a narrow native initialization HLE; the full Dolphin
DSP emulator is not linked. No DSP ROM, microcode bytes or generated game code are
added to Git.

The STM implementation references Dolphin's
[`IOS/STM/STM.cpp`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/IOS/STM/STM.cpp)
and `STM.h` at the same revision for EVENTHOOK, RELEASE_EH and IPC error semantics.
These files credit the Dolphin Emulator Project (GPL-2.0-or-later); intact review
copies remain in `local/references/dolphin/IOS/STM/`. The native SDK bridge uses
Swiftshine/key's `revolution/IPC/ipcclt.c` and `OS/OSStateTM.c` as ABI references,
cross-checked against the verified DOL. No Dolphin emulation kernel is linked.

NAND configuration serialization references Dolphin's `Core/SysConf.cpp` and
`Common/SettingsHandler.cpp` at the pinned revision. The latter credits Treeki for
the original settings implementation; its full notice remains in the local review
copy. Filesystem command semantics reference `Core/IOS/FS/FileSystemProxy.cpp`.
The SDK's `FS/fs.c`, `NAND/NANDCore.c`, `SC/scsystem.c` and `OS/OSInterrupt.c` in
Swiftshine/key provide the request and callback ABI, checked against RK5E01.
The new backend uses host POSIX file descriptors and synthetic initial configuration.
It does not import a console NAND dump, credentials, tickets or cryptographic identity.
The initial KD RTC command references Dolphin's `Core/IOS/Network/KD/NetKDTime.cpp`
(Dolphin Emulator Project, GPL-2.0-or-later) and the SDK's `NWC24Time.c`.

The extracted disc backend references Dolphin's `Core/IOS/DI/DI.cpp`, `DI.h`,
`Core/HW/DVD/DVDInterface.cpp`, `DVDInterface.h`, `DiscIO/DiscUtils.h` and
`DiscIO/WbfsBlob.cpp` at the same pinned revision. These files credit the Dolphin
Emulator Project (GPL-2.0-or-later); their notices remain intact in ignored local
review copies. The new reader and IOS handlers are GPL-3.0-only and do not link
Dolphin's drive emulator. Swiftshine/key's DVD and IPC source, community function
boundaries and the verified local DOL establish the SDK request/callback ABI.
The WBFS allocation table is used only to audit this revision's disc geometry;
the runtime reads extracted files without a WBFS or decryption dependency.

Disc title metadata serialization references Dolphin's `Core/IOS/ES/Views.cpp`,
`Formats.h`, `Formats.cpp` and `ES.cpp` at the pinned revision. Original notices
credit the Dolphin Emulator Project (GPL-2.0-or-later) and remain in the local review
copies. This project's implementation validates the mounted title and serializes
its extracted metadata; it does not import Dolphin's ES kernel or cryptography.
The GPIO AV encoder model uses the pin/register definitions from Dolphin's
`WII_IPC.h` and `WII_IPC.cpp`, cross-checked against RK5E01's I2C writer.
The new bit-level decoder and synthetic tests are GPL-3.0-only. No game-specific
encoder payloads or ticket contents are embedded in these sources.

The CP/PI FIFO transport and PE register handshake reference Dolphin's
[`VideoCommon/CommandProcessor.cpp`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/VideoCommon/CommandProcessor.cpp),
`CommandProcessor.h`, `PixelEngine.cpp`, `PixelEngine.h`, `Core/HW/ProcessorInterface.cpp`,
`Core/HW/GPFifo.cpp` and `Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp` at the
same pinned revision (Dolphin Emulator Project, GPL-2.0-or-later). Local review copies
retain their original notices. New transport code is GPL-3.0-only; no Dolphin graphics
backend is linked. The synchronous WPAR BNE interpretation follows the limitations
explicitly described in Dolphin's GPFifo source; cycle-accurate bus timing is not claimed.

The GX stream adapter uses the packet formats and register interfaces in the pinned
Aurora `lib/gx/command_processor.cpp`, `regs.cpp`, `regs.hpp` and `gx.hpp`. Vertex-size
calculation is adapted from Aurora's `calc_vtx_size`; its MIT notice credits Luke Street
and remains in `vendor/wiicompiled/aurora-main/LICENSE`. The new framing, guest-memory
bounds, runtime integration and tests are GPL-3.0-only. Aurora sources are linked
unchanged, including their original licenses; no Mario Kart-specific GX HLE is enabled.

The XFB conversion follows the coefficient/normalization rules in Dolphin's
`VideoCommon/TextureConversionShader.cpp` (`WriteXFBEncoder`) and copy control fields
in `VideoCommon/BPMemory.h` at revision `db2afcbdf8f5175a6b8414d9f5e21c978775f651`.
These sources credit the Dolphin Emulator Project (GPL-2.0-or-later); local reference
copies retain their notices. Dolphin itself marks exact XFB rounding as needing further
hardware verification. VI framebuffer layout follows its `Core/HW/VideoInterface` model.
The new CPU conversion, VI observer and tests are GPL-3.0-only. The GPU adapter calls
Aurora's existing MIT-licensed copy/readback/frame APIs (Luke Street), referencing
`lib/gx/readback.cpp`, `lib/gfx/readback/{readback.hpp,copy_layout.hpp}` and
`lib/dolphin/gx/GXFrameBuffer.cpp`. Aurora's source and notices remain unchanged.

The native scheduler boundary follows Swiftshine/key's `revolution/OS/OSThread.c`,
`OSContext.h`, `OSInterrupt.c` and `OSStateTM.c` (CC0), cross-checked against the verified
RK5E01 DOL. WarioRecomp's `runtime/src/hle/os/os_scheduler.cpp`, `os_context.cpp` and
`host_context`/fiber interfaces (GPL-3.0, repository revision pinned above) informed the
host-continuation approach. Its game-specific addresses, fallback behavior and thread
manager are not imported. New scheduler/IRQ bridge code is GPL-3.0-only.
The STM no-dimming request also references the already pinned Dolphin
`Core/IOS/STM/STM.cpp` (GPL-2.0-or-later), retaining the existing local source notice.

The WPAD bridge links the unchanged Aurora `aurora_wpad`/`aurora_pad` libraries
(`lib/revolution/wpad`, `lib/dolphin/pad` and the SDL input backend), under the preserved
MIT license crediting Luke Street. Its guest ABI and report layout were checked against
Swiftshine/key `revolution/WPAD/WPAD.c` and `WPAD.h` (CC0) and the verified RK5E01 DOL.
WarioRecomp `runtime/src/hle/input/wpad.cpp` and `kpad.cpp` (GPL-3.0, pinned revision above)
were reviewed for native input boundaries; no Wario addresses, KPAD layouts or gameplay
patches are imported. The new bridge, audits and integration probe are GPL-3.0-only.

`runtime/src/input/controller_mapping.cpp` adapts Aurora's default button/axis
tables to Kirby's positional Xbox face-button layout. It retains Luke Street's
2022 copyright and the complete MIT notice in the source. No upstream source is
modified for this mapping; saved Aurora mappings retain precedence over defaults.

Additional repositories supplied during bring-up are reviewed in
[docs/references.md](docs/references.md), with pinned revisions and reuse decisions.
No source from WiindowsPlay, NWiiRecomp or ResortCompiled has been imported or linked.
The directory/name cleanup preserves the licenses and notices of all existing components.

Stage 19 reuses WiiCompiled's existing AXWii mixer and native audio backend (GPL-3.0),
with the owned `ax-hardware.patch` applied only to the ignored runtime snapshot.
The task loader and IRQ boundary use verified RK5E01 symbols and DOL bytes. No foreign
game globals or boot patches are enabled. AXWii microcode identity and mailbox behavior
were cross-checked against the pinned Dolphin `DSPHLE/UCodes/{ROM,AX,AXWii}.cpp` and
`HW/DSP.cpp` references (GPL-2.0-or-later). SDK alarm and WPAD information layouts
reference Swiftshine/key (CC0). Their original notices remain with the dependencies.

The direct-draw and palette adapter calls Aurora's unchanged MIT-licensed pipeline,
shader-info, texture and FIFO interfaces. Palette load fields reference the pinned
Dolphin `VideoCommon/BPMemory.h`. NAND usage semantics reference its
`Core/IOS/FS/{FileSystemProxy.cpp,HostBackend/FS.cpp}` and the SDK FS request layout;
local reference copies retain the Dolphin copyright/license notice. Authored adapters
and tests are GPL-3.0-only.

Stage 20 adds `patches/aurora/preserve-efb.patch` to the pinned Aurora recording
backend (MIT, copyright Luke Street). It exposes an explicit operation to load
existing EFB attachments after a host submission boundary. Kirby uses it when
resuming GX after readback or VI presentation. The build copies the pinned Aurora
sources and their license to ignored `local/native-workspace/aurora`, applies the
patch there, and leaves the vendor checkout unchanged.

Stage 22 reuses Aurora's MIT `copy_tex`, `resolve_pass_into`, and readback tile
packing paths. The owned BP format/layout adapter was checked against Dolphin's
[`BPMemory.h`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/VideoCommon/BPMemory.h)
and [`TextureDecoder.h`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/VideoCommon/TextureDecoder.h)
(GPL-2.0-or-later, reference only). FS creation wire offsets come from Swiftshine's
CC0 `src/revolution/FS/fs.c`; temporary-directory initialization was checked against
Dolphin's `Core/IOS/FS/FileSystemProxy.cpp`. No new vendor code was copied into the
owned runtime. `user.kirby.isfs` stores the four permission/attribute bytes on local
host files/directories; it is authored metadata, not a console filesystem image.

Stage 23 checks GetAttr and Rename wire behavior against Swiftshine's CC0
`src/revolution/FS/fs.c` and Dolphin's GPL-2.0-or-later
`Core/IOS/FS/{FileSystemProxy.cpp,HostBackend/FS.cpp}` (reference only). The owned
implementation uses POSIX descriptor operations and persistent authored xattrs;
it preserves IOS's file-basename and open-source-file rename rules. No additional
upstream implementation was copied into the owned runtime.

The native `launcher/main.cpp` links the existing SDL3 and Dear ImGui components
already provided by Aurora, under their preserved zlib and MIT licenses. Its code
is authored GPL-3.0-only. ZeldaWWHDRecomp's SDL3/ImGui frontend and documentation
organization were reviewed as references; no MPL source, graphics or branding was
copied. Pinned review links and reuse boundaries are in `docs/references.md`.

`patches/wiicompiled/ax-retail-memory.patch` adapts WiiCompiled's GPL-3.0 AX mixer
memory windows to the configured retail MEM2 extent. Its source notices remain
unchanged. The synthetic MEM2 voice test uses RK5E01 AXPB offsets from
Swiftshine/key's CC0 `src/revolution/AX/AXPB.h`; no disc audio is included in tests.

`patches/aurora/xfb-yuyv-readback.patch` extends Aurora's existing compute readback
shader and layout metadata. Aurora's MIT license and copyright notices remain in
the pinned source tree. The authored YUYV packing follows the same Dolphin-referenced
coefficients and quantization as this project's CPU encoder; no game shader or
game asset is included. The CPU encoder remains the synthetic regression oracle.
