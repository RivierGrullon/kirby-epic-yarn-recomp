# RK5E01 Rev 0 bring-up results

Initial records dated 2026-11-12. The target device is a Lenovo Legion Go with Bazzite;
later graphical runs were verified on an Acer Nitro AN515-55, as recorded below.
Paths in stages 1–16 are historical; the current source layout is described in
[the runtime guide](../runtime/README.md).
Stage 28 records native Fountain Gardens loading, movement and jumping through
virtual PAD input. Physical controller validation remains pending.

## Verified inputs

The supplied 7z archive contains one 3,831,496,704-byte WBFS. 7-Zip completed extraction
with no CRC error. Dolphin Flatpak 2609a exported the DATA partition. `sys/boot.bin`
identifies `RK5E01` and revision `0`. The exported `files/` directory contains 2,804 files
and remains local for native DVD/file loading.

| Property | Observed value |
| --- | --- |
| DOL size | 9,340,736 bytes |
| SHA-1 | `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130` |
| SHA-256 | `a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079` |
| Header entry / `__start` | `0x80004050` |
| First startup call / `__init_registers` | `0x80004210` |
| r2 / SDA2 | `0x808EE8E0` |
| r13 / SDA | `0x808E73E0` |
| r2 initialization pair address | `0x8000428C` |
| r13 initialization pair address | `0x80004294` |
| DOL sections | 2 text, 8 data |
| Translation memory window | `0x80000000` through `0x81800000` (24 MiB MEM1) |

The SHA-1 matches the pinned Swiftshine/key `config/RK5E01/config.yml`.
Startup instructions establish the bases directly, cross-checked with
`src/revolution/OS/__start.c`. Raw word evidence and section ranges are retained only
in `local/dol-report.json`. Every mapped function lies in a DOL text section.
The MEM1 window covers the DOL sections and BSS; this does not determine the runtime's
arena layout, MEM2 allocation or stack policy.

No standalone REL was found by extension or structurally valid unlinked REL headers
among extracted files. This scan does not exclude compressed or embedded modules.
Super Paper Mario's `wiicompiled/projects/spm/recomp.yml` accepts a separate prelinked
REL via `inputs.rel`; its `tools/prelink_spm_rel.py` is game-specific. No SPM load
address or prelink assumption has been imported into Kirby.

## Toolchain and translation

The host initially had no .NET, Clang, CMake or Ninja on PATH. The rootless Podman
toolchain supplies SDK 8.0.428, runtime 8.0.34, Clang 18.1.3, CMake 3.28.3 and Ninja 1.11.1.

The first translator build with Ubuntu's SDK 8.0.134 failed with **CS9057**: Tomlyn 2.10.1's
source generator requires Roslyn 4.9, while that SDK supplies 4.8. Replacing the base
image with the pinned official .NET 8 SDK image resolved it. The translator then built
with zero warnings and zero errors. The original failure log remains in
`build-logs/translator-build-sdk-8.0.134-failed.log`. No analyzer check was disabled.

The preparation suite passes all 8 tests on the host and inside the container.

```bash
python3 tools/bootstrap.py
python3 tools/extract.py "/run/media/system/Games/Kirby's Epic Yarn (USA) (En,Fr,Es).7z"
bash tools/container.sh --build
bash tools/container.sh python3 tools/kirby.py doctor
bash tools/container.sh python3 tools/kirby.py translate --threads 2
bash tools/container.sh python3 tools/build_native.py --jobs 2
```

WiiCompiled translates 32,737 functions from 32,697 community function boundaries plus
recursive discovery. Its metadata reports **0 unsupported instructions** and **0 invalid
SSA functions**. Translation took approximately 60 seconds. One speculative discovery
candidate at `0x00000060` was rejected as outside the loaded image; this diagnostic is
retained in `build-logs/translate.log` and is not proof that every dynamic target is covered.

`generate-data-init` emitted the DOL initializer and 32,697 named guest symbols.
`emit-build-shards` emitted 72 base shards. Their generated code and embedded game data
are excluded from Git. The empty Kirby native registry deliberately excludes other
games' hooks from discovery and native ABI inference.

The standalone entry function compiled successfully with:

```bash
bash tools/container.sh clang++ -std=c++17 -march=x86-64-v3 \
  -DMKW_TRANSLATED_GUEST_REGION=0 \
  -I vendor/wiicompiled/runtime/include -I . \
  -c generated/functions/func_80004050.cpp -o build/kirby-entry.o
file build/kirby-entry.o
```

The result is an ELF 64-bit x86-64 relocatable object, not a runnable game.
WiiCompiled uses guest-region index 0 as identity mapping for non-Mario-Kart games;
it does not mean this USA Kirby dump was treated as a PAL revision.

## Runtime integration

Native CMake configuration succeeds with the pinned WiiCompiled runtime and Aurora.
The build helper recreates upstream's sibling `runtime/` and `generated/` layout using
a runtime source snapshot and a generated-directory link under ignored
`local/native-workspace/`, leaving dependency checkouts unchanged and preserving licenses.

The first full compile failed at `runtime/game/runtime_config_values.cpp`: Clang could
not find `generated/RuntimeConfig.h`. This reproduced by building that object directly.
The original runtime-directory symlink made `runtime/..` resolve back into the upstream
checkout; CMake also missed the generated data initializer and selected its symbol-table
stub. Replacing that directory symlink with a physical runtime source snapshot corrects
both path resolutions. Failure evidence remains in
`build-logs/compile-missing-generated-config.log` and
`build-logs/reproduce-missing-generated-config.log`.

After that correction, all 72 game shards, data initialization, guest registrations,
the runtime and Aurora compiled and linked successfully. The final incremental build
used `--jobs 4 --translated-jobs 4`; the documented two-job default is more conservative.
`build/native/WiiCompiled` is an ELF 64-bit x86-64 PIE executable. The final build log is
`build-logs/compile.log`.

The runtime still requires Kirby-specific product glue and verified native registrations.
For example, WiiCompiled's `runtime/include/system_bridge.h` has default entry
`0x800060A4`; Wario's counterpart uses `0x80006124`. Kirby's verified entry is
`0x80004050`. Wario also contains game-specific rope fixes and motion behavior.
These values and patches have not been applied to Kirby. Correcting the entry alone
would not establish correct HLE binding or initialization.

## Stage 1 blocker: Mario Kart-specific product bootstrap (resolved in stage 2)

The built native executable was invoked twice with a bounded, windowless startup probe:

```bash
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Both attempts exited with status 1 before the timeout, reporting:

```text
Runtime error: --headless needs --race[=<course id>] or --race-cpu
[runtime] CPU context unavailable; registers could not be dumped.
[runtime] Signal 11 (fault address 0x30)
```

Logs: `build-logs/native-startup.log` and `build-logs/native-startup-repeat.log`.
`runtime/src/main.cpp` rejects a headless invocation unless Mario Kart's `BootRace`
was requested. Its error path then also faults during host cleanup. No PowerPC context
was created and no Wii call or Kirby instruction was observed. This is a **host runtime
bootstrap/integration blocker**, not a translation failure, link failure, or a crash
observed inside Kirby. It is specifically a headless probe; a graphical boot has not
been tested, and this result alone does not establish its behavior.

Enabling a Mario Kart race option would not make a valid Kirby test: the default entry
and native registrations still target Mario Kart. The runtime needs a Kirby product
bootstrap and audited HLE bindings before a meaningful game boot. The next concrete
step is to introduce Kirby-specific entry/product glue for `0x80004050`, map the startup
OS/DVD/VI/GX/input native boundaries to RK5E01 with verified symbols and implementations,
and rerun translation with that native index. The premature-shutdown fault should also
be fixed when separating the generic headless bootstrap from `BootRace`.

No first frame, title, menu, controller response or Fountain Gardens milestone has been
observed. Native executable construction and command-line startup do not count as a
successful game boot. Stage 1 stopped at this reproducible integration boundary.

## Stage 2: independent Kirby bootstrap

`runtime/kirby_main.cpp` is now the process entry. The legacy runtime entry is renamed
for linkage, preserving reusable diagnostics without calling Mario Kart's `RuntimeMain`
or `SystemBridge::Initialize`. A small GPL-licensed source patch is applied only to
`local/native-workspace/runtime`, leaving the pinned checkouts unchanged.

The bootstrap loads the embedded DOL into 24 MiB MEM1. Cached `0x80000000`, uncached
`0xC0000000` and physical `0x00000000` views share its backing store. Alias conversion
is confirmed by Swiftshine/key's `src/revolution/OS/OSAddress.h`. No inherited 128 MiB
MEM2 allocation, foreign constructor range, stack address or game-specific patch is used.
Only the actual disc's identity is copied to low memory; full apploader/BootInfo setup
remains incomplete. The game establishes its own stack and SDA registers.

Registration isolation initially exposed another inherited path: hand-written HLE could
register as `FunctionKind::BaseTranslated`, bypassing a native-only filter. Registry
validation correctly failed on the foreign `PPCMfhid2_HLE` entry at `0x8012E630`.
Both `AbiTrampoline` constructor paths are now gated, including these handwritten
"translated" overrides. Generated bulk registrations remain available. In a normal
probe, 583 foreign runtime registrations are rejected. Mario Kart's graphics-argument
rewrite is also neutralized. No Kirby HLE hook has been enabled yet.

All **11 tests** pass inside the build container. Three are native integration checks:
the actual translated Kirby entry survives attempted native and legacy overrides;
the game's compiled register initializer returns the verified r2/r13 and a valid stack;
and invalid command-line arguments exit 2 without the old renderer-shutdown fault.
These three tests were observed failing against the stage 1 executable before implementation.

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 2
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-init
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

The register probe successfully executes `__init_registers` at `0x80004210` and returns:

```text
r2=0x808ee8e0 r13=0x808e73e0 r1=0x809e4278; stack is inside MEM1
```

Evidence: `build-logs/bootstrap-tests-before.log`, `bootstrap-tests.log`, and
`kirby-init.log`. This is real statically recompiled game code executing natively,
but not a rendered game milestone. Graphics, audio and controller input are not started.

`tools/audit_hooks.py` indexes the pinned upstream runtime and compares its canonical
Mario Kart map names with Kirby's function symbols. Of 788 indexed registrations,
48 have one name-based Kirby address candidate and 740 lack a named match. This does
not verify implementation or ABI compatibility. For example `__init_hardware` resolves
to Kirby `0x80004360`, but its foreign implementation is still not activated. Conditional
and alternate registrations in the source index explain why its count differs from
the actual compiled registration attempts. The report is `local/hook-audit.tsv`.

## Stage 2 blocker: PI interrupt initialization (resolved in stage 3)

The full headless entry probe now reaches Kirby's `__start` at `0x80004050` and enters
`__OSInterruptInit` at `0x80668220`. Two independent runs exit **1**, without a timeout
or the previous host cleanup crash, with the same guest access violation:

```text
[kirby] blocked: guest memory access 0xcc003004:
  Memory access violation at 0xCC003004 (+0x4) :: MMIO write blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x80668250 ctr=0x0 r1=0x809e4220
```

Logs: `build-logs/kirby-startup.log` and `kirby-startup-repeat.log`.
The reported context PC is coarse entry tracking, not the faulting instruction PC.
The LR belongs to `__OSInterruptInit`; independent inspection of the verified DOL
locates the matching PI mask store at `0x80668270`.

Swiftshine/key's `src/revolution/OS/OSHardware.h` identifies `0xCC003004` as `PI_INTMR`.
Its `OSInterrupt.c` implementation initializes this mask for EXI, AI, DSP and memory
interrupts before masking individual sources and installing the external interrupt
handler. WiiCompiled's `MemoryInline::Write32Slow` explicitly rejects this non-GPU
MMIO write. This is a **Wii hardware/runtime boundary during actual guest startup**,
not a translator, linker or command-line failure.

The upstream `SetInterruptMask` replacement explicitly forces return 0 rather than
emulating the controller, and other interrupt helpers reference foreign guest state.
Retargeting those addresses alone would not validate Kirby's interrupt behavior.
The next concrete work is a reviewed Kirby interrupt HLE layer or stateful PI controller
integration, starting with `__OSInterruptInit`, `SetInterruptMask`, mask state and external
interrupt delivery. That implementation must be indexed before retranslating any
intercepted SDK functions. No blanket MMIO write bypass or fake successful OS initializer
has been introduced. Work stops at this reproducible hardware integration boundary.

No first frame, title, menu, controller response or Fountain Gardens has been observed.

## Stage 3: interrupt-register model

`runtime/kirby_interrupts.cpp` now supplies stateful IRQ registers. A small second patch
routes the runtime's 16/32-bit checked slow paths to it. Other MMIO accesses keep their
existing errors. No SDK function is intercepted, no game instruction is changed, and
no source retranslation or foreign HLE registration was needed.

| Interface | Implemented register subset |
| --- | --- |
| PI | Cause `0xCC003000`, mask `0xCC003004`, big-endian halfword reads |
| MI | 16-bit IRQ mask/flags `0xCC00401C`/`0xCC00401E` |
| DSP | IRQ mask and acknowledgment bits in 16-bit CSR `0xCC00500A` |
| AI | IRQ mask/acknowledgment bits in CSR `0xCD006C00` |
| EXI | IRQ fields of channel CSRs `0xCD006800`, `0xCD006814`, `0xCD006828` |
| IPC | Write-only Broadway IRQ flags/mask `0xCD000030`/`0xCD000034` |

Addresses and bit assignments were checked against Kirby's `OSHardware.h`,
`dsp_hardware.h`, `ai_hardware.h`, `EXIHardware.h` and `OSInterrupt.c`.
State transitions were cross-checked with Dolphin revision
`db2afcbdf8f5175a6b8414d9f5e21c978775f651`:
[PI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/ProcessorInterface.cpp),
[MI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/MemoryInterface.cpp),
[DSP](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DSP.cpp),
[AI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/AudioInterface.cpp),
[EXI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/EXI/EXI_Channel.cpp),
[IPC](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/WII_IPC.cpp).

The model preserves source/mask state and aggregates device IRQ levels into PI.
Acknowledgment is write-one-to-clear where specified; MI flags instead use direct
writes. Unsupported DSP reset/run, audio start, EXI device selection/transfer and IPC
request operations are rejected without partially modifying state. Cold state has
no active video source because this bootstrap has not initialized a VI device.
There is no device timing or guest exception delivery yet; `Pending()` only exposes
the interrupt condition for future integration.

The standalone C++ test passes **49 checks**, compiled with `-Wall -Wextra -Werror`.
They cover masks, source propagation, acknowledgment independence, halfword endianness,
atomic rejection, channel separation and reset. The complete Python/native suite passes
**12 tests**. The new integration test was first observed failing before implementation.

The isolated probe initially returned from interrupt initialization but failed its
handler check. Its prerequisite `OSExceptionTable` had not been set by `OSExceptionInit`.
For this probe only, the harness now supplies that prerequisite. The variable at
`0x808E5E9C` is verified independently against the community object symbol and the actual
SDA-relative `lwz` in `__OSSetExceptionHandler` at `0x806627A0`. Normal `__start` does not
use this fixture; it executes its own SDK exception setup. The probe then runs Kirby's
compiled initializer and verifies the handler at SDK exception slot 4, restored stack,
PI mask `0xF0`, IPC mask `0x40000000`, masked device sources and no pending interrupt.

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 2
bash tools/container.sh cmake --build build/native --target kirby_interrupts_test --parallel 2
bash tools/container.sh ctest --test-dir build/native -R '^kirby_interrupts$' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-interrupt-init
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

The probe prints `Kirby interrupt initializer returned; external handler installed;
device sources masked`. Evidence: `build-logs/kirby-interrupt-init.log`,
`interrupt-bootstrap-tests.log`, `interrupt-ctest.log`, and `interrupt-unit-build.log`.
The original failures are retained in `interrupt-tests-before.log` and
`interrupt-unit-before.log`. Game data and generated code remain ignored by Git.

## Stage 3 blocker: EXI transfer control

Two full native startup probes now pass interrupt initialization and exit **1** on
the same unsupported EXI read, before the 15-second timeout:

```text
[kirby] blocked: guest memory access 0xcd00680c:
  MMIO read blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x80662244 r1=0x809e4240
[kirby] PI mask=0xf0 pending=0
```

Logs: `build-logs/kirby-stage3-startup.log` and `kirby-stage3-startup-repeat.log`.
The DOL call at `0x80662240` in `OSInit` targets `EXIInit` at `0x8069E210`.
`EXIInit` polls channel 0 transfer-control register `0xCD00680C` for the `TSTART` bit
before proceeding to channel setup and device identification. Its initial matching
load is at `0x8069E230`; context PC remains coarse entry tracking. The register layout
and polling loop are confirmed by `EXIHardware.h` and `EXIBios.c`.

This is a device transaction interface outside the implemented IRQ subset. The next
concrete step is an EXI control/transfer backend with reviewed boot-device responses,
including device identification and IPL/RTC/SRAM dependencies. Returning idle or fake
successful transfer data alone would not implement those protocols.

No rendered frame, menu, controller response or Fountain Gardens milestone has been
observed. The executable remains a headless bring-up build.


## Stage 4: EXI immediate transport and dynamic MMIO reads

The EXI model now maintains each channel's CSR, DMA address/length, transfer control
and immediate-data registers. Selection and clock fields retain state, transfer start
clears only after a supported operation, and TC acknowledgment remains independent of
its interrupt mask. The configured topology has an internal IPL device, empty card
slots, no SP1 accessory and no channel-2 debug accessory.

Absent-device immediate behavior follows Dolphin's
[`IEXIDevice`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/EXI/EXI_Device.cpp):
reads return zero, writes discard bytes, and duplex leaves the data word unchanged.
This response applies only to explicitly empty slots. The internal IPL instead
collects a four-byte, big-endian command across immediate writes. Reselecting it starts
a new command; acknowledging TC without changing selection does not. IPL payloads other than the EUART subset below, and
all DMA execution, still raise a diagnostic including the command and transfer registers;
they never claim completion. SRAM, RTC, fonts and persistent storage are not implemented.
The command framing is cross-checked against
[`EXI_DeviceIPL.cpp`](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/EXI/EXI_DeviceIPL.cpp).

The first native test after adding this transport exposed a separate runtime problem.
`EXISelect` calculates the channel-register address dynamically. Its `lwz` at
`0x8069DCE0` was emitted as `MemoryInline::FlatRead32`, which directly dereferenced
`0x1000CD006800` and raised SIGSEGV. Both the isolated EXI probe and normal `__start`
reproduced this failure; logs are `exi-probe-flat-read-crash.log` and
`exi-flat-read-crash.log` under `build-logs/`. The instruction and its preceding
channel-stride calculation were checked against the verified DOL.

`runtime/wiicompiled-flat-mmio.patch` routes flat loads in the MMIO window to the
existing width-specific slow handlers. Ordinary RAM loads retain the direct path.
This fixes the runtime contract for dynamically addressed registers without modifying
generated game code or enabling a signal handler that creates zero-filled device pages.
The standalone `kirby_flat_mmio_test` first reproduced SIGSEGV with the original header;
after the patch, it checks dynamic device reads, unsupported-read rejection, RAM
endianness and preservation of the direct RAM path. This shared header change requires
rebuilding translated shards; it does not require rerunning the translator.


After the flat-read correction, `EXISync` reached the read-only DI configuration
register at `0xCD006024`. The SDK helper's load is in the DOL at `0x80662934`.
It now returns the reviewed non-Triforce configuration value `1`, with writes still
rejected, following Dolphin's
[DVD interface initialization and MMIO registration](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DVD/DVDInterface.cpp).
No disc command, DMA, status or file-access behavior is implied by this single register.
The original failure is retained in `build-logs/exi-di-config-before.log`.

`OSRegisterVersion` then exercised the SDK's EUART output through EXI. The transport
now handles the `0xB0000000` F2/F3 initialization handshake, the `0x30000100` queue query,
and `0xB0000100` character writes. This is checked against Kirby's `EUART/euart.c`,
`EXI/EXICommon.c` and Dolphin's IPL EUART handling. A synchronous host sink accepts
nonzero bytes, ignores SDK padding and prints complete lines prefixed `[kirby:EUART]`.
The queue reports empty because bytes are consumed immediately. The diagnostic line
buffer is bounded; invalid operations and all unrelated IPL payloads remain explicit
errors. `build-logs/exi-euart-before.log` records the original unsupported F2 write.


### Native verification

The EXI probe executes the compiled `EXIInit` at `0x8069E210`, after the same register
and exception-table prerequisites as the interrupt probe. It checks stack restoration,
EUART readiness/output, deselected channels, no active transfer and no unmasked IRQ.
It exits **0**, completes **85 transfers**, and prints the game's actual EXI version
message. The full Python/native suite passes **13 tests**. Both standalone CTest targets
pass, including **118 register/transport checks** compiled with strict warnings.

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native \
  --target kirby_interrupts_test kirby_flat_mmio_test --parallel 2
bash tools/container.sh ctest --test-dir build/native \
  -R '^kirby_(interrupts|flat_mmio)$' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-exi-init
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Evidence is retained in `build-logs/kirby-exi-init.log`, `exi-bootstrap-tests.log`,
`exi-ctest.log`, `exi-unit-build.log`, and `compile.log`. The original failing tests are
in `exi-tests-before.log`, `exi-native-before.log`, `flat-mmio-before.log`,
`di-config-before.log`, and `euart-before.log`. No SDK function was replaced or skipped;
the community source describes the protocol while the executable runs the user's
verified DOL translation. The vendor checkout remains unmodified.

## Stage 4 blocker: VI clock during SI initialization

Two full startup probes print the game's EXI and SI diagnostic messages, complete
**163 EXI transfers**, and exit **1** before the 15-second timeout:

```text
[kirby] blocked: guest memory access 0xcc00206c:
  Memory access violation at 0xCC00206C (+0x2) :: MMIO read blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x8069fef0 ctr=0x8067b358 r1=0x809e4210
[kirby] PI mask=0xf0 pending=0
[kirby] completed EXI transfers=163
```

Logs: `build-logs/kirby-stage4-startup.log` and `kirby-stage4-startup-repeat.log`.
The DOL call at `0x80662244` enters `SIInit` (`0x8069F560`), which calls
`SISetSamplingRate` (`0x8069FEB0`) at `0x8069F5B0`. The actual failing instruction is
`lhz r4, 0x206C(r3)` at `0x8069FF40`, with `r3 = 0xCC000000`. These instructions were
checked directly in the verified DOL. The recorded PC remains coarse entry tracking;
LR points after the preceding `VIGetTvFormat` call and is not the failing instruction.

`VI/vihardware.h` identifies this halfword register as `VI_VICLK` and its bit 0 as the
27/54 MHz selector. `SI/SISamplingRate.c` uses it to calculate controller polling rates.
This is a video/SI device-model boundary, not a translator or linker failure. A printed
SI version message does **not** mean SI initialization has finished.

The next concrete step is to establish a reviewed VI boot state and implement the SI
poll/transfer and completion behavior it feeds. Returning a clock constant alone would
not validate SI initialization or controller input. SRAM/RTC, complete boot metadata,
IOS/IPC, disc access, graphics and input integration remain pending. No first frame,
menu or Fountain Gardens milestone has been observed; this is still a headless build.


## Stage 5: VI/SI startup and SRAM/RTC storage

`runtime/kirby_video_si.cpp` provides the NTSC boot register state, advancing beam,
VI interrupt comparators/reset, channel registers, communication RAM, SI status/masks
and type-query completion for four disconnected GameCube ports. Device errors remain
errors: an empty port produces NOREP/COMERR and TC, preserves communication RAM, and
never invents controller IDs or input. Halfword/word accesses retain big-endian ordering.
VI clock changes affect half-line duration; polling uses the SI X setting. The initial
54 MHz NTSC preset and state transitions are checked against the pinned Dolphin
[VI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/VideoInterface.cpp),
[SI](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/SI/SI.cpp)
and [disconnected-device](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/SI/SI_DeviceNull.cpp)
implementations and Kirby's SDK headers. Those source files are references, not linked
emulator components.

MMIO entry points update elapsed host time and propagate VI/SI levels into PI. This is
not an independent event scheduler: guest external-exception delivery and queued SI
callbacks are still pending. The native SI initializer returns after configuring
`SIPOLL = 0x01EC0200`, enabling the SI PI mask and completing the first device query.
The remaining queued queries have not been demonstrated to drain in a complete boot.
The standalone model checks all four disconnected channels. No frame is rendered.

The full startup next requested SRAM through EXI command `0x20000100`, DMA address
`0x808FE0A0`, length `0x40`. `runtime/kirby_ipl.cpp` now provides the 4-byte RTC and
64-byte SRAM layout, immediate and DMA payloads, Wii RTC flags and persistent settings.
SRAM starts with synthetic English/stereo defaults and a computed SDK checksum. Only
the 64 settings bytes persist, in ignored `local/user/sram.bin`; writes use temporary
file replacement, existing settings load unchanged, and invalid file lengths fail
without overwrite. The structure and checksum were checked against `OSRtc.h` and
Dolphin's [SRAM implementation](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/Sram.cpp).

The runtime supplies RTC seconds through its existing Broadway time-base backend,
reading upper/lower words with rollover retry and dividing by the existing 60.75 MHz
contract. Command receipt snapshots the RTC. There is no independent epoch or clock
implementation in the new storage model. EXI validates the complete DMA span, alignment,
command direction and storage range before changing data or signaling completion.
The memory resolver accepts only the three mapped aliases of verified 24 MiB MEM1.
Unsupported ROM/font access and other device DMA still fail explicitly.

Preparation now emits `SiInit`, `SramInit` and `SramControl` metadata. `Scb` at
`0x808FE0A0`, size `0x54`, is checked against both the community object symbol and the
actual adjacent `lis`/`addi` sequence in `__OSInitSram`. The SRAM probe invokes the
compiled initializer, checks stack restoration, synchronized/unlocked control state,
and compares all 64 guest bytes with persistent storage. It exits **0**. Full `__start`
uses its own normal SDK initialization, not the isolated probe's exception-table fixture.

### Reuse audit

Wario's SI initialization and sampling-rate hooks skip device setup and polling, so they
cannot establish the requested SI behavior. They remain disabled. Both reference ports
contain a reusable SDL audio backend and AX/DSP implementation. However, their audio
initializers call `MarkDspInitialized`/`ResetDspTaskGlobals`, which write game-specific
addresses in `ax_internal.h`; these addresses have not yet been retargeted for Kirby.
The existing storage/IOS implementations also need address, heap and callback audits.
A matching SDK function name alone does not authorize those writes.

### Verification

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test kirby_ipl_test kirby_exi_dma_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-si-init
bash tools/container.sh build/native/WiiCompiled --probe-sram-init
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

All **15 Python/native tests** and **five CTest targets** pass. The C++ models cover
119 interrupt/EXI checks, 58 VI/SI checks, 10 storage checks and 46 DMA checks; the flat
memory regression also passes. Evidence: `build-logs/stage5-native-tests.log`,
`stage5-ctest.log`, `si-init.log`, `sram-init.log`, `stage5-tests-build.log`, and
`compile.log`. Original failures are retained in `stage5-before.log`,
`video-si-before.log`, `stage5-startup.log` and `ipl-before.log`.

## Stage 5 startup stop: audio clock and DSP initialization

Two final full-startup probes exit **1** at the same access, before the 15-second timeout:

```text
[kirby] blocked: guest memory access 0xcd800180:
  Memory access violation at 0xCD800180 (+0x4) :: MMIO read blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x806637e4 r1=0x809e4230
[kirby] PI mask=0xf8 pending=1
[kirby] completed EXI transfers=165
```

Logs: `build-logs/stage5-startup-final.log` and `stage5-startup-final-repeat.log`.
The actual instruction is `lwz r5, 0x180(r4)` at `0x806635C8` in `__AIClockInit`
(`0x806635A0`), with `r4 = 0xCD800000`. Its caller is `__OSInitAudioSystem`
(`0x806637C0`). The instruction was verified in the DOL. The SDK proceeds from audio
clock programming to DSP reset, code upload, DMA and mailbox synchronization; answering
this first read alone would not implement that initialization.

At this stage, boot-memory setup and audio clock access were still missing. Stage 6
below supersedes this stop. CPU interrupt delivery, DSP and IOS services remain open.


## Stage 6: boot memory, IOS56 buffers and audio clock

The native loader previously supplied only the disc ID. In particular, the SDK bus-clock
word was zero and MEM2 was unmapped. `runtime/kirby_boot_memory.cpp` now validates and
loads the extracted boot header, BI2, FST and TMD. The local TMD requests IOS title
`0000000100000038` (IOS56) for title `00010000524B3545` (RK5E). Signature format and
identity are checked; this is not cryptographic TMD authentication.

The memory profile follows Dolphin's pinned
[IOS56 table](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/IOS/VersionInfo.cpp),
[IOS memory setup](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/IOS/IOS.cpp)
and [Wii boot setup](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/Boot/Boot_BS2Emu.cpp).
Addresses and SDK consumers were cross-checked against Kirby's `OSHardware.h`, `OS.c`
and the verified DOL. No Wario/Mario Kart heap constants are used.

| Item | Value |
| --- | --- |
| Physical MEM1 / MEM2 | 24 MiB / 64 MiB |
| MEM2 aliases | `0x10000000`, `0x90000000`, `0xD0000000` |
| Usable MEM2 arena | `0x90000800` to `0x935E0000`, end exclusive |
| IPC buffer | `0x935E0000` to `0x93600000`, end exclusive |
| IOS reserved range reported by IOS56 | `0x93600000` to `0x93620000` |
| IOS compatibility version / date | `56.5662` / `0x030110` |
| Bus / CPU / time-base frequency | 243 / 729 / 60.75 MHz |
| Local FST / BI2 address | `0x817EBE60` / `0x817E9E60` |

The TMD identifies the IOS major number, not its installed revision. The explicit
56.5662 compatibility profile comes from Dolphin; no installed IOS firmware revision
was inferred from the disc. Mapping physical RAM does not make IOS-reserved memory
available to the game allocator.

The native loader places the aligned FST and BI2 at the top of MEM1, then restricts
both boot-info and SDK arena-hi fields below them. This is a native allocation policy;
the disc's apploader is not executed. Preparation reads r1's actual initialization pair
(`0x80004284`, initial stack `0x809E4278`) and reserves an additional 8 KiB plus alignment
headroom. It also checks all DOL sections and BSS. Metadata must remain above every
protected range. The FST's Wii word-scaled size/capacity, names, directory parents and
subtree bounds are checked before any guest writes. The local FST is 82,312 bytes;
BI2 is 8,192 bytes. Their installed bytes match the extracted files exactly.

`--probe-boot-memory` checks MEM2 alias coherence at both ends and rejects the first
address outside each 64 MiB mapping. It also invokes Kirby's real `__OSInitIPCBuffer`,
`IPCInit`, `IPCGetBufferLo` and `IPCGetBufferHi`, verifying the original SDK consumes
the supplied IPC bounds and restores its stack. This proves buffer setup, not mailbox
transport, IOS device services, client heap creation or interrupt delivery.

`runtime/kirby_clock.cpp` supplies the three reviewed Hollywood audio-clock registers
at `0xCD800180`, `0xCD8001CC` and `0xCD8001D0`. Dolphin's
[IPC register map](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/WII_IPC.cpp)
accepts these addresses without modeling an analog PLL. Our narrow implementation
retains the SDK's configuration fields and rejects unreviewed bits/addresses. Clock
readiness describes the completed register configuration; it does not generate audio
or execute DSP instructions. The existing WiiCompiled time-base backend supplies the
SDK's delay loop. No foreign game hooks are enabled.

`--probe-audio-clock` executes the compiled `__AIClockInit` for both arguments 1 and 0,
checks the final configuration and restored stack, and confirms each call waits at
least 3 ms. Full startup now passes this sequence and the DSP work-buffer copies.

### Stage 6 verification and stop

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test \
  kirby_ipl_test kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-boot-memory
bash tools/container.sh build/native/WiiCompiled --probe-audio-clock
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

All **19 Python/native tests** (10 synthetic preparation tests and 9 native checks)
and **seven CTest targets** pass. The new standalone tests cover 53 boot-memory checks
and 10 clock checks. Native probes exit **0**. Both final normal-startup runs exit **1**
at the same DSP write before the 15-second timeout.

Evidence: `build-logs/prepare-stage6.log`, `compile.log`, `stage6-tests-build.log`,
`stage6-ctest.log`, `stage6-native-tests.log`, `boot-memory-probe-final.log`,
`audio-clock-probe-final.log`, `stage6-startup-final.log` and
`stage6-startup-final-repeat.log`. The memory probe intentionally triggers and catches
three out-of-range access errors; their diagnostic dumps are expected in its log.

The full-startup stop is now:

```text
[kirby] blocked: guest memory access 0xcc005012:
  Memory access violation at 0xCC005012 (+0x2) :: MMIO write blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x80663818 r1=0x809e4230
[kirby] PI mask=0xf8 pending=1
[kirby] completed EXI transfers=4
```

The verified instruction is `sth r0, 0x5012(r3)` at `0x80663820`, word `B0035012`,
with `r0 = 0x43` and `r3 = 0xCC000000`. This programs DSP ARAM size in
`__OSInitAudioSystem`, before reset, DMA and mailbox synchronization. The reported PC
remains the outer compiled entry; the instruction location comes from the DOL and
translated function, not from that coarse PC. The retail console-type field now causes
normal startup to skip debug EUART output, reducing EXI transfers from 165 to 4.
The isolated EXI probe still exercises diagnostic output with its SDK-only prerequisites.

The next concrete change is to retarget the existing WiiCompiled DSP/audio initialization
and its guest-global writes for Kirby, including direct-call routing. Simply permitting
the ARAM size write would immediately encounter unsupported DSP reset and DMA. CPU
interrupt delivery and IOS requests remain required before the active objective is
complete. No frame, title, menu, controller input or Fountain Gardens has been observed.


## Stage 7: DSP boot and IPC client initialization

The compiled `__OSInitAudioSystem` now returns. Its original calls and work-buffer
copies remain translated; no foreign audio initializer or DSP globals are enabled.
The reusable WiiCompiled AX initializer writes Mario Kart addresses, and those DSP/AX
globals are not named in the current Kirby symbol map. Enabling it would bypass the
required address audit. Its mixer remains available for later retargeting.

`runtime/kirby_dsp.cpp` implements a narrow native HLE for the SDK initialization
microcode and its register transport. Behavior was checked against the pinned Dolphin
[DSP interface](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DSP.cpp),
[DSP control](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DSPHLE/DSPHLE.cpp),
[initialization microcode HLE](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DSPHLE/UCodes/INIT.cpp)
and [mailbox behavior](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/DSPHLE/MailHandler.cpp).
It does not interpret DSP instructions or implement the game's audio mixer.

Preparation verifies `DSPInitCode` at `0x808B8E20`, length `0x80`, lies in a loaded
DOL data section and is the actual source of the copy to `0x81000000` in
`__OSInitAudioSystem`. The source bytes are captured locally after DOL initialization.
Clearing DSP init checks all 128 uploaded bytes against that snapshot before accepting
the code. Unknown uploads and DSP commands still fail. Game-derived bytes stay local.

The model starts halted with DSP init set, clears reset on write, retains interrupt
masks/W1C status, and makes ROM/init mail visible only when unhalted. Reading the low
mailbox half consumes the message; high-half reads alone do not. The verified init
code produces `0x80544348`. Its load-busy flag clears after 130 time-base ticks.

ARAM DMA copies actual bytes between validated physical MEM1 and Wii MEM2 spans in
both directions. It checks both complete spans before copying. Completion occurs after
246 CPU cycles per 32 bytes, converted to the existing 60.75 MHz clock, and raises the
ARAM status/PI source according to its mask. The tested configuration is AR_INFO low
nibble 3; other ARAM configurations and audio sample DMA remain unsupported.

The native `--probe-dsp-init` sets the SDK arena-hi prerequisite through Kirby's own
setter, fills the work buffer with a sentinel, then calls its compiled audio initializer.
It verifies two real 32-byte DMAs, one verified init upload/message, restored stack,
final halted/reset state and restoration of all 128 sentinel bytes. It also checks the
MEM2 DMA destination against the DOL microcode. Full `__start` independently reaches
this same sequence without the probe's setup.

### Memory setup and IPC client

The next accesses were MI setup at `0xCC004020` and `0xCC004010`, then the read-only
PI revision at `0xCC003024`. The runtime now accepts the reviewed clear and all-regions
read/write setting `0xFF`; attempts to enable memory protection still fail because native
protection enforcement is not implemented. PI reports Dolphin's revision C profile
`0x246500B1`. Memory backing and bounds remain the previously verified retail layout.

WiiCompiled intentionally translates `rfi` as a C++ return (`PpcLifter.cs`). Thus the
SDK's real-mode BAT reconfiguration does not install hardware page translations here;
the native loader supplies the address aliases. This does not establish working guest
exception returns or thread context switching. Those require explicit integration.

The IPC register subset now includes the PPC message pointer, control register,
acknowledgment/reply masks, their IRQ acknowledgment semantics and the empty ARM reply
register. Submitting an IOS request (X1) is still rejected before mutating control or
inventing a reply. Register semantics follow Dolphin's
[IPC control layout](https://github.com/dolphin-emu/dolphin/blob/db2afcbdf8f5175a6b8414d9f5e21c978775f651/Source/Core/Core/HW/WII_IPC.h)
and `WII_IPC.cpp`.

`--probe-ipc-client` invokes the compiled `__OSInitIPCBuffer` and `IPCCltInit`, with the
same verified interrupt-table prerequisite used by the interrupt probe. It checks return
value zero, stack restoration, handler 27 pointing to `IPCInterruptHandler` at
`0x806A8CA0`, mailbox control `0x38`, and the enabled PI IPC bit. The current IPC buffer
lo advances to `0x935E2000`: this DOL allocates **8192 bytes**, whereas the decompilation
source currently declares a smaller heap. The executed DOL is authoritative.

### Current stop and verification

Normal startup now reaches the first actual IOS request:

```text
[kirby] blocked: guest memory access 0xcd000004:
  Memory access violation at 0xCD000004 (+0x4) :: MMIO write blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x806a909c r1=0x809e41f0
[kirby] PI mask=0x40f8 pending=1
[kirby] completed EXI transfers=4
[kirby] DSP boot: DMA=2 init=1 mail=1
[kirby] unsupported IOS request at 0x135e0020 command=1 open="/dev/stm/immediate" mode=0
```

This is request submission, not the earlier `IPCCltInit` configuration write at the same
MMIO address. The request structure is inspected with bounded reads; no response or
file descriptor is fabricated. WiiCompiled's current STM hook only writes synthetic
handles into Mario Kart globals, so it cannot be enabled unchanged for Kirby.

Reproduction:

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test \
  kirby_ipl_test kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test kirby_dsp_boot_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh build/native/WiiCompiled --probe-dsp-init
bash tools/container.sh build/native/WiiCompiled --probe-ipc-client
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

All **21 Python/native tests** (10 synthetic preparation tests and 11 native probes)
and **eight CTest targets** pass. The expanded register suite has 135 checks; the new
DSP suite has 42. Both new native probes exit **0**. Two complete startup attempts exit
**1** at the same STM request before the 15-second timeout.

Evidence: `build-logs/prepare-stage7.log`, `stage7-tests-build.log`,
`stage7-final-build.log`, `stage7-ctest.log`, `stage7-native-tests.log`,
`dsp-init-probe-final.log`, `ipc-client-probe-final.log`, `stage7-startup-final.log`
and `stage7-startup-final-repeat.log`. Earlier MI, PI and IPC-client configuration
stops are retained in `stage7-startup.log`, `stage7-memory-startup.log` and
`stage7-pi-startup.log`. The upstream source checkouts remain unchanged.

The next concrete task is an STM/IOS service interface with real handle ownership and
IPC acknowledgment/reply delivery to Kirby, followed by the guest interrupt/context
integration needed to resume synchronous requests. The active objective remains
incomplete: queued SI callbacks, CPU interrupt delivery, IOS services and video
presentation still need verification. No game frame or playable milestone is claimed.

## Stage 8: native IOS transport and STM lifetime

The original synchronous `__ios_Ipc2` unconditionally calls `OSSleepThread` after
submitting a request. Completing an MMIO write alone cannot resume it without guest
context switching. The native override now replaces this common transport boundary,
preserving the translated public IOS wrappers, their physical buffer addresses and
the original SDK allocator. This is functional native IOS service handling, not an
implementation of Hollywood mailbox interrupts or the PowerPC exception scheduler.

`tools/kirby.py prepare` verifies the transport symbol `0x806A8FA0` (size `0x268`),
its `lwz r3,disp(r13)` reference to `hid` at `0x808E4734`, and its branch to `iosFree`
at `0x806AA7B0`. It also cross-checks the STM globals against the DOL's initializer
loads/stores and the unnamed command-2 wrapper used as `IOS_Close`. Generated constants
stay local. The registration macro bypasses MKW's regional address map, and the bootstrap
admits only this exact address/name pair. Foreign registration rejection remains tested.

The new service owns distinct `/dev/stm/immediate` and `/dev/stm/eventhook` descriptors.
EVENTHOOK retains its request until an event or RELEASE_EH; startup produces no synthetic
event. RELEASE_EH writes a zero event and queues the original request's completion.
The bridge invokes the original callback with an isolated CPU context, then frees its
request through the translated SDK heap. Synchronous responses free their request before
returning. Immediate asynchronous responses are queued, not recursively invoked from
inside their submitting wrapper. Delivery currently occurs only at another IOS call
boundary or an explicit drain; there is no continuous event pump yet.

Unimplemented service names stop with a diagnostic instead of masquerading as absent
devices. Synchronous event waiting, close with an outstanding event, other STM ioctls,
NAND/ES operations and physical button integration remain explicit unsupported cases.
No existing WiiCompiled STM hook or Mario Kart global was imported.

Verification on the local native ELF (BuildID `2738835d79e59784485ed4a607f6312f4566ba96`):

- Full translation repeated: 32,737 generated functions, zero unsupported instructions
  and zero invalid SSA functions. The build graph excludes the overridden transport,
  linking 32,736 translated functions plus the native implementation.
- All 22 Python/native tests pass, including 12 actual native executable probes.
- All nine project CTest targets pass. The new standalone IOS suite has 23 checks for
  descriptor ownership, deferred event completion, cancellation, invalid memory, pathname
  bounds, duplicate hooks, double close and unsupported services.
- `--probe-stm-init` executes Kirby's original initializer, confirms its ready/registered
  globals and pending request, then completes 256 original IOS open/close cycles without
  exhausting the 8192-byte heap. It cancels the event through the original unregister
  function and executes the original callback, preserving the caller context and freeing
  the request.
- Two full `--headless` runs independently complete STM and exit 1 at the same next
  unsupported service. Neither run reaches a frame or menu.

```text
[kirby] blocked: Unimplemented IOS open: /dev/fs
[kirby] pc=0x80004050 lr=0x806a942c r1=0x809e41e0
[kirby] PI mask=0x40f8 pending=1
[kirby] DSP boot: DMA=2 init=1 mail=1
[kirby] STM: ready=1 pending request=0x935e0020
```

The displayed PC remains the outer translated entry; LR identifies the IOS_Open return
site. Swiftshine's `SCInit` calls `NANDInit`, whose `ISFS_OpenLib` opens `/dev/fs` before
initializing ES, obtaining the current title ID and its data directory. The next concrete
implementation is a local NAND/ES service backend for that sequence and configuration
loading. It must use Kirby's verified title metadata and keep mutable data under `local/`.
CPU interrupt delivery, SI callback scheduling and framebuffer presentation also remain
unfinished; the broader objective is still active.

Commands used:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh build/native/WiiCompiled --probe-stm-init
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test \
  kirby_ipl_test kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test \
  kirby_dsp_boot_test kirby_ios_service_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Logs are in `build-logs/stage8-prepare.log`, `stage8-translate.log`, `stage8-shards.log`,
`stage8-compile.log`, `stage8-stm-probe-final.log`, `stage8-native-tests.log`,
`stage8-tests-build.log`, `stage8-ctest.log`, `stage8-startup-final.log` and
`stage8-startup-repeat.log`. A Konsole window was opened with the previously verified
executable at the user's request; it shows diagnostic output and remains open on exit.

## Stage 9: NAND configuration, IOS callbacks and boot compatibility

`runtime/kirby_nand.cpp` now backs IOS files with native POSIX file descriptors rooted
at `local/user/nand/`. Read, write, seek, EOF, cursor/size queries and close retain real
state. Paths must be absolute NAND paths, with no traversal or host symlink components.
Missing files return FS_ENOENT; unknown devices and unimplemented commands still stop
explicitly. File permissions currently enforce the requested access mode; Wii UID/GID
metadata, file creation/deletion commands and general ISFS ioctls are not implemented.

The backend creates synthetic `shared2/sys/SYSCONF` (16384 bytes) and
`title/00000001/00000002/data/setting.txt` (256 bytes) only when missing. Existing files
are preserved. Defaults select USA/English/NTSC, stereo, widescreen and progressive
preferences; these are console settings, not evidence of a displayed video mode.
The product-settings encoder preserves the SDK's encrypted-NUL termination behavior.
The title data directory is `/title/00010000/524b3545/data`, derived from the validated
TMD. ES GET_TITLE_ID (`0x20`) and GET_DATA_DIR (`0x1D`) validate their vectors and write
big-endian results. No Mario Kart title ID or filesystem globals are reused.

The first full run with file support timed out while SCCheckStatus remained busy.
A deliberate SIGABRT sample showed LR `0x806622EC` in OSInit; the memory snapshot had
`BgJobStatus = 1` and `ItemNumTotal = 0`. A separate probe drained the six original SDK
callbacks and successfully parsed 11 entries. That distinguished a missing callback
delivery boundary from a malformed NAND file or a missing ES response.

The second audited native override is `OSRestoreInterrupts` at `0x806681C0`, size
`0x24`. Preparation checks its symbol and SHA-256
`54db610a580003e2db23e460425f4662f4f4e7a4da3ae6f89a632df9e52b427a` against the local DOL.
The native implementation retains its CR0/SO, r3/r4/r5 and MSR effects, then drains
completed IOS callbacks when MSR.EE is enabled. Callback registers are isolated and
the callback runs with EE cleared. Reentrancy is guarded. This does not deliver arbitrary
PI interrupts or implement guest thread switching. Full translation changed 440 files
and left **zero inlined copies** of the overridden function; shard generation alone
would have left those copies active. The graph now links 32,735 translated functions
plus two native replacements, with zero unsupported instructions or invalid SSA functions.

The subsequent boot calls open owned KD request/time descriptors, set the STM slot-LED
policy and synchronize KD's volatile RTC counter with ioctl `0x17`. No network sockets
are opened. Other KD commands, shutdown/download scheduling, universal time queries and
persistent `misc.bin` counter updates remain unsupported. These are the boot operations
observed and implemented, not a complete WiiConnect24 service.

### Apploader minimum IOS version

After network initialization, a read from `0xCC005036` initially appeared to be the next
device failure. The caller was actually OSFatal, reached through OSInit's IOS compatibility
check (ERROR #002). The loader had left `MINIMUM_IOS_VERSION` at `0x80003188` zero.
The DSP read was a secondary operation while trying to display that fatal error; it was
not patched to return a fabricated value.

The supplied `sys/apploader.img` has SHA-256
`2f6f2ae03ba24f9464637f6c271a9a8e032f113b80fd8e3966bcba53b78ca9ef`, entry `0x812004D0`,
code size `0x1D04` and trailer size `0x3BD80`. Its initialization sequence at file offset
`0x67C` stores `0x0038151D` to `0x80003188` (the store is at offset `0x688`).
`InstallBootData` now validates the header/ranges, finds exactly one matching initializer,
checks its IOS slot against the TMD and its version against the native IOS56 profile,
then installs that value. The existing `0x0038161E` IOS version satisfies the game's
original comparison. No game instruction or compatibility branch was modified.
Native startup now also requires the extracted apploader; DOL translation remains
independent of it.

### Verification and next stop

All **23 Python/native tests** (10 preparation tests and 13 native executable probes)
and **11 CTest targets** pass. The new NAND and IOS/NAND suites each have 18 checks;
boot-memory checks increased to 63 and IOS service checks to 28. Invalid apploader
headers, ambiguous/missing initializer sequences, unsupported IOS requirements and title
identity are checked before any boot-memory writes. The native configuration probe
confirms no callback executes with EE disabled, then loads/parses the files through
the original wrappers after enabling interrupts: 11 items, language 1, six callbacks.
The bootstrap probe verifies the register effects of interrupt restoration for both
initial EE states and zero, positive and negative requests.

The final ELF BuildID is `9b1046c5d3f160b6ce53f95a4872456aa18946f1`. Two full native
startup runs exit **1** at the same next service:

```text
[kirby] blocked: Unimplemented IOS open: /dev/di
[kirby] pc=0x80004050 lr=0x806a942c r1=0x809e4200
[kirby] DSP boot: DMA=2 init=1 mail=1
[kirby] STM: ready=1 pending request=0x935e0020
[kirby] SC: items=11 IOS callbacks=6 KD RTC synchronized=1
```

The next concrete task is DI/DVD request handling and validated reads from the extracted
disc, retaining the original filesystem offsets and complete `files/` contents. General
interrupt delivery, queued SI callbacks, audio playback and framebuffer presentation
also remain unfinished. No first frame, title, menu or playable input is claimed.

Commands executed:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh build/native/WiiCompiled --verify-bootstrap
bash tools/container.sh build/native/WiiCompiled --probe-system-config
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test kirby_ipl_test \
  kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test kirby_dsp_boot_test \
  kirby_ios_service_test kirby_nand_test kirby_ios_nand_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Final logs: `build-logs/stage9-translate.log`, `stage9-shards.log`, `stage9-compile.log`,
`stage9-native-tests.log`, `stage9-tests-build.log`, `stage9-ctest.log`,
`stage9-config-probe-final.log`, `stage9-startup-final.log` and `stage9-startup-repeat.log`.
Intermediate evidence remains in `stage9-nand-startup.log`, `stage9-config-wait-trace.log`,
`stage9-wait-artifacts/` and `stage9-kd-startup.log`. The forced-abort snapshots contain
local game memory and stay ignored. Their upstream crash-report game label is still
RMCP01; the loaded image, symbol references and native bootstrap are verified RK5E01.

Files changed in this stage: new `runtime/kirby_nand.h`, `runtime/kirby_nand.cpp`,
`tests/nand_test.cpp` and `tests/ios_nand_test.cpp`; updated `runtime/kirby_ios_service.h`,
`runtime/kirby_ios_service.cpp`, `runtime/kirby_ios_bridge.h`,
`runtime/native-src/kirby_ios.cpp`, `runtime/kirby_boot_memory.h`,
`runtime/kirby_boot_memory.cpp`, `runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`,
`tools/kirby.py`, `tests/boot_memory_test.cpp`, `tests/ios_service_test.cpp`,
`tests/test_native_bootstrap.py`, `README.md`, `THIRD_PARTY.md`,
`runtime/native-src/README.md` and this report. Dependencies remain unchanged.

## Extracted disc and IOS DI (stage 10)

`/dev/di` now owns an IOS descriptor and serves read-only DATA partition requests.
The native loader has already selected that partition; this is not an implementation
of partition authentication or IOS partition switching. The original DVD low-level
initialization, request construction, callback contexts and IPC heap remain translated.
No additional native overrides or game instruction patches were introduced.

`runtime/kirby_disc.cpp` validates the FST tree, names, parent/end indices, duplicate
paths, host file sizes, extent overlap and configured capacity. It opens each component
with `openat`/`O_NOFOLLOW` and rejects non-regular files. Word offsets become 64-bit byte
offsets. A failed or unbacked read cannot partially overwrite the destination buffer.
The 2,804 extracted files map to their original FST offsets, alongside the header, BI2,
apploader, DOL and FST extents. Only trailing padding up to a 32-byte DMA boundary is
synthesized, clipped before the next real extent. Other gaps remain unsupported.
This is an extracted-file view, not a byte-exact reconstruction of unused disc space.

Implemented DI requests are inquiry `0x12`, disc ID `0x70`, partition read `0x71`,
cover register/status `0x7A`/`0x88`, cover interrupt acknowledgment `0x86`, out-of-bounds
raw read `0x8D`, retail REPORT KEY failure `0xA4`, and request error `0xE0`.
DI success is transfer-complete **1**, not IOS success 0. Drive failures return **2**
and latch the specific sense code; request error returns and clears it. An in-bounds
raw read still stops explicitly instead of interpreting its offset as a DATA offset.

### Geometry and original drive checks

The profile uses retail single-layer capacity `0x118240000` (4,699,979,776 bytes),
the value in Dolphin's pinned `DiscIO/DiscUtils.h`. The supplied WBFS uses 512-byte
host sectors and 2,097,152-byte allocation blocks. Its table has 1,826 allocated
logical blocks, the last at index 2,233: referenced data ends no later than
`0x117400000`, below single-layer capacity. This supports the standard single-layer
profile, consistent with Dolphin's size inference for WBFS; the container format
does not record an exact original ISO size. No geometry from Wario was imported.
The metadata-only audit stays in `local/disc-capacity-audit.json`.

Reproduce the allocation audit from this repository:

```bash
python3 - <<'PY'
from pathlib import Path
import struct
images = list(Path('local/images').glob('*.wbfs'))
assert len(images) == 1
with images[0].open('rb') as f:
    header = f.read(12)
    assert header[:4] == b'WBFS'
    host_sector, block = 1 << header[8], 1 << header[9]
    count = (143432 * 2 * 0x8000 + block - 1) // block
    f.seek(host_sector + 256)
    table = struct.unpack('>' + str(count) + 'H', f.read(count * 2))
used = [i for i, value in enumerate(table) if value]
print('Allocated blocks:', len(used), 'last logical index:', max(used))
print('Referenced end upper bound:', hex((max(used) + 1) * block))
PY
```

Kirby's `__DVDCheckDevice` requests 32 raw bytes at word offset `0x460A0000`
(byte offset `0x118280000`). This exceeds the configured capacity and produces
the actual drive failure contract: interrupt type 2, sense `0x52100`. Its subsequent
REPORT KEY command produces invalid-command sense `0x52000`, matching retail drive
behavior. The unchanged game checks and their original callbacks now complete.

### Callback regression found and fixed

The first implementation reached a busy wait at `__DVDCheckDevice+0x90`, guest
`LR=0x806764D0`; the callback flag at `0x808E45E8` remained zero. A bounded forced-abort
snapshot confirmed this, and an isolated enabled-interrupt DVD request reproduced it.
The original `__ios_Ipc2` restores interrupts after queuing a request. The native
replacement previously delivered ready callbacks only before submission or on a later
`OSRestoreInterrupts` call. An SDK caller that immediately spun had no such later call.

The transport now also delivers ready completions before returning when MSR.EE is set.
Delivery preserves the caller's CPU context, uses the original callback and heap free,
and remains guarded against recursion. Requests submitted with EE cleared remain queued.
The regression failed before the change and passes afterward; this does not establish
general CPU interrupt delivery, guest thread scheduling or SI callback execution.

The native probe invokes these community-map functions, audited against the actual DOL:

| Function | RK5E01 address | Verification |
| --- | --- | --- |
| `DVDLowInit` | `0x80676890` | Named community function |
| `DVDLowReadDiskID` | `0x80676B00` | Anonymous map function; command `0x70`, 32-byte output, SDK callback ABI |
| `DVDLowRead` | `0x80678410` | Anonymous map function; command `0x71`, length and word offset arguments |
| `__BS2DVDLowCallback` | `0x806750A0` | `stw r3,disp(r13)` and return; result address derived as `0x808E6024` |

Preparation checks the sizes and SHA-256 digests of the three anonymous functions
before emitting probe constants. `--probe-disc` executes 16 original SDK reads of the
disc ID, FST and banner (992 bytes), compares them with extracted data, alternates
masked/enabled interrupts and verifies callback/context reuse beyond the four SDK slots.

### Verification and current stop

The native x86-64 ELF has BuildID `94a9cf1d3fac3c45e5ab3c67bbeb8d1bb78de12f`.
All **24 Python/native tests** (10 preparation and 14 native probes) and **12 CTest
suites** pass. The new disc suite has 39 checks, including offsets beyond 4 GiB,
adjacent file padding, gaps, truncation, symlinks, malformed FSTs, IOS descriptor
ownership, guest span validation, capacity-dependent errors and sense-code lifetime.

Two full native runs now exit **1** at the same next dependency:

```text
[kirby] blocked: Unimplemented ES ioctlv: 27
[kirby] pc=0x80004050 lr=0x806aa11c r1=0x809e3fb0
[kirby] DSP boot: DMA=2 init=1 mail=1
[kirby] STM: ready=1 pending request=0x935e0020
[kirby] SC: items=11 IOS callbacks=12 KD RTC synchronized=1
[kirby] disc: files=2804 reads=0 bytes=0
```

Decimal 27 is ES `0x1B`. The named `ESP_DiGetTicketView` at `0x806FF460` prepares
this command with one input and one output vector. The next concrete step is to
validate `disc/DATA/ticket.bin` against the mounted title and implement its ticket-view
request ABI, then rerun normal startup. Authentication and other ES operations must
not be replaced with fabricated success values. No game frame, menu, controller input
or Fountain Gardens has been observed. Normal startup has not yet read game files;
the verified file transfers above belong to the isolated native SDK probe.

Commands executed:

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 --target WiiCompiled \
  kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test kirby_ipl_test \
  kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test kirby_dsp_boot_test \
  kirby_ios_service_test kirby_nand_test kirby_ios_nand_test kirby_disc_test
bash tools/container.sh build/native/WiiCompiled --probe-disc
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Final logs are `build-logs/stage10-final-build.log`, `stage10-native-tests.log`,
`stage10-ctest.log`, `stage10-disc-unit.log`, `stage10-regression-after.log`,
`stage10-startup-final.log` and `stage10-startup-repeat.log`. The before-fix regression,
timeout trace and forced-abort snapshots remain in ignored `build-logs/stage10-*`.
Translation was not repeated: existing function bodies and override addresses did not
change. All game bytes, generated translations, snapshots and executables remain ignored.

Files added: `runtime/kirby_disc.h`, `runtime/kirby_disc.cpp`, `tests/disc_test.cpp`.
Files updated: `runtime/kirby_ios_service.h`, `runtime/kirby_ios_service.cpp`,
`runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`, `runtime/native-src/kirby_ios.cpp`,
`runtime/native-src/README.md`, `tools/kirby.py`, `tests/test_native_bootstrap.py`,
`README.md`, `THIRD_PARTY.md` and this report. Vendor revisions and licenses are unchanged.

## Disc ES metadata and AV encoder (stage 11)

Normal native startup now reaches **`main` → `gfl::Init` → `GXInit`**. The previous
ES ticket-view failure and a subsequent AV encoder GPIO failure are resolved.
The current stop is a 16-bit write to `0xCC000002`, the command-processor control
register, from `GXSetCPUFifo`. No framebuffer, title screen or menu has been observed.

### ES requests and extracted metadata

The local ticket is 676 bytes, RSA-2048 format, version 0; the TMD is 520 bytes
with one content entry. Both identify title `00010000524B3545`. Native boot already
validates the TMD's IOS56 requirement. The new `DiscTitleMetadata` validates format,
length, title identity and the complete content table before mounting metadata.
These checks are structural; they do not verify signatures or authorize another title.
Runtime boot now requires the extracted `ticket.bin` as well as `tmd.bin`.

Implemented ES ioctlv operations:

| Request | Behavior |
| --- | --- |
| `0x1B` DI ticket view | One input and one output vector; 216-byte view from the supplied v0 ticket, or the mounted disc ticket when input size is zero |
| `0x39` DI TMD size | Returns the mounted TMD's actual byte length |
| `0x3A` DI TMD | Validates the requested/output lengths and copies the complete extracted TMD |
| `0x16` consumption | Returns zero usage entries only for this mounted ticket with all eight time-limit enable fields clear |

The view consists of a padded version field followed by ticket ID through time limits.
It excludes the encrypted title-key field. Unknown tickets and enabled time limits stop
explicitly because consumption tracking for them is not implemented. Invalid vectors,
counts, spans and sizes return `-1017` without partial output. Title launch, installation,
signature verification and cryptographic services remain separate unsupported operations.
No ticket contents or keys were added to source control.

`--probe-es-metadata` calls the original library open/close and metadata wrappers:
`ESP_InitLib` (`0x806FF1F0`), `ESP_DiGetTicketView` (`0x806FF460`), `ESP_DiGetTmd`
(`0x806FF510`), the consumption wrapper (`fn_806FF740`) and `ESP_CloseLib`
(`0x806FF250`). Addresses come from the community map after the DOL hash check.
The anonymous consumption wrapper was checked against its ioctlv `0x16`, eight-byte
ticket ID input, two output vectors and original register ABI. The probe compares
both ticket-view paths and the TMD with extracted bytes, checks the consumption count
and confirms stack restoration and library close.

### AV encoder GPIO

After the ES requests, startup first stopped reading `0xCD8000C4` inside Kirby's I2C
writer. This is Broadway GPIO direction; `0xCD8000C0` is output and `0xCD8000C8` input.
Pins `0x4000` and `0x8000` carry the AV encoder's clock and data. The sender uses
slave byte `0xE0` (seven-bit address `0x70`, write direction).

The new `AveGpio` model in `kirby_video_si.cpp` decodes START/STOP, samples bytes on
clock edges, acknowledges accepted bytes on the ninth clock and releases SDA afterward.
It respects GPIO direction and pull-ups. Completed per-register payloads are retained
in wire order. Other slave addresses NACK; encoder reads and unreviewed GPIO writes
remain explicit failures. No permanent low input or unconditional acknowledgment is used.
The payloads are configuration state only: analog encoding and frame presentation are
not implemented.

`--probe-ave` invokes the original I2C writer (`fn_8067BBD0`, size `0x534`) with
two test transactions and verifies register payloads, six ACKs, stack preservation and
both interrupt-enable states. Full startup executes **13 writes and 102 ACKs** through
the same decoder before reaching GX. No additional native function overrides were added.

### Verification and current stop

The final x86-64 ELF BuildID is `9378b32d258b22d6642b972744320fba3bef36c1`.
All **26 Python/native tests** (10 preparation and 16 native probes) and **14 CTest
suites** pass. New suites cover 36 ES metadata/vector cases and 21 GPIO/encoder cases.
The first test compilation caught implicit 64-to-32-bit narrowing in two synthetic
fixture fields; explicit conversions fixed it without changing compiler diagnostics.

Two runs of the final executable exit **1** at the same write:

```text
[kirby] blocked: guest memory access 0xcc000002: ... MMIO write blocked (non-GPU)
[kirby] pc=0x80004050 lr=0x80685f5c r1=0x809e41e0
[kirby] stack frame=0x809e41e0 return=0x80684e80
[kirby] stack frame=0x809e4200 return=0x80640558
[kirby] stack frame=0x809e4220 return=0x80640460
[kirby] stack frame=0x809e4240 return=0x8063ae04
[kirby] stack frame=0x809e4250 return=0x8064765c
[kirby] stack frame=0x809e4260 return=0x800041b4
[kirby] SC: items=11 IOS callbacks=13 KD RTC synchronized=1
[kirby] AV encoder: writes=13 ACKs=102
```

The bounded PowerPC backchain added to diagnostics resolves to `GXInit+0x90`,
`fn_806404B0+0xA8`, `fn_80640414+0x4C`, `gfl::Init+0x60`, `main+0x1C` and
`__start+0x164`. This establishes a normal game initialization path rather than a
secondary `OSFatal` graphics path. The upstream error's “non-GPU” wording describes
its unsupported MMIO route; the address itself is a graphics command-processor register.

The next concrete task is Kirby-specific GX/FIFO integration with Aurora. WiiCompiled's
`runtime/src/hle/gx/gx_init.cpp` supplies reusable host GX calls, but its guest FIFO,
GXData addresses, sizes and interrupt globals are Mario Kart-specific. They cannot be
enabled unchanged. Kirby's named `GXInit` (`0x80684DF0`), `GXSetCPUFifo` (`0x80685EB0`)
and `GXSetGPFifo` (`0x80686090`) provide verified starting points for the ABI/data audit.
Graphics presentation, general CPU interrupt delivery, queued SI callbacks and playable
controller input remain unfinished. Normal startup still has zero DATA file reads at
this point; the prior disc-read milestone remains an isolated native SDK test.

Commands executed:

```bash
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh build/native/WiiCompiled --probe-es-metadata
bash tools/container.sh build/native/WiiCompiled --probe-ave
bash tools/container.sh cmake --build build/native --parallel 4 --target WiiCompiled \
  kirby_interrupts_test kirby_flat_mmio_test kirby_video_si_test kirby_ipl_test \
  kirby_exi_dma_test kirby_boot_memory_test kirby_clock_test kirby_dsp_boot_test \
  kirby_ios_service_test kirby_nand_test kirby_ios_nand_test kirby_disc_test \
  kirby_es_metadata_test kirby_ave_gpio_test
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

Evidence is retained in ignored `build-logs/stage11-final-build.log`,
`stage11-final-native-tests.log`, `stage11-ctest.log`, `stage11-es-test.log`,
`stage11-es-probe.log`, `stage11-ave-unit.log`, `stage11-ave-probe.log`,
`stage11-startup-final.log` and `stage11-startup-final-repeat.log`.

Added files: `runtime/kirby_es_metadata.h`, `runtime/kirby_es_metadata.cpp`,
`tests/es_metadata_test.cpp`, `tests/ave_gpio_test.cpp`. Updated files:
`runtime/kirby_ios_service.h`, `runtime/kirby_ios_service.cpp`, `runtime/kirby_video_si.h`,
`runtime/kirby_video_si.cpp`, `runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`,
`tools/kirby.py`, `tests/test_native_bootstrap.py`, `README.md`, `THIRD_PARTY.md`
and this report. Dependency checkouts remain clean; generated code, dumps and the
executable remain local and ignored. No retranslation was needed for these changes.


## Stage 12: original GX FIFO and pixel-engine initialization

The previous `0xCC000002` stop is resolved without a native replacement for any Kirby
GX SDK function. The original `GXSetCPUFifo` (`0x80685EB0`, size `0x1E0`) and
`GXSetGPFifo` (`0x80686090`, size `0x290`) now configure the native CP/PI register model.
These function identities and ranges come from the pinned RK5E01 community symbols;
the complete DOL remains revision-hash checked. No Mario Kart FIFO or GXData address
is used. Kirby's published `GXFifoObj` is 128 bytes, but this implementation preserves
the original SDK's structure accesses instead of casting that object to a host struct.

The first run with CP support progressed to `__GXPEInit` (`0x80687EC0`, size `0x64`),
where the PE control read at `0xCC00100A` was still unsupported. Adding the reviewed
PE configuration registers and interrupt handshake resolved that read. Both original
FIFO setters and PE initialization now return inside the normal game's `GXInit`.

New transport behavior:

- CP low/high halves retain the Wii physical-address mask and 32-byte alignment.
  CPU FIFO base/end/write pointer are mapped at PI offsets `0x0C`, `0x10`, `0x14`.
  Status comes from actual queued distance, pointers, read enable and breakpoint state.
- Gather writes accumulate bytes in order, commit complete 32-byte bursts to checked
  guest RAM, wrap at the inclusive FIFO end, and update the linked CP write pointer
  and distance. Overflow, invalid pointers, mismatched linked FIFOs and unmapped RAM
  fail explicitly. A consumer must accept a burst before its read pointer advances.
- CP interrupt levels reach PI source `0x800`. PE token/finish have independent pending
  state, enables and write-one acknowledgments, driving PI `0x200`/`0x400`. This does
  not add general guest CPU exception dispatch or renderer-generated completion events.
- All five `GX_HLE_FIFO_Write*` exports now belong to Kirby. Both generated direct
  calls and memory slow paths use them. The upstream definitions are renamed only
  for `gx_fifo.cpp` and `gx_stubs.cpp` through CMake source properties; no vendor files
  or translated shards are edited. `nm` confirms the five Kirby exports and separately
  named unused upstream definitions in the linked executable.
- The original SDK writes SPR 921 through the leaf at `0x80661480`, selecting physical
  address `0x0C008000`. WiiCompiled previously warned and retained only a generic SPR
  shadow. The new small runtime patch routes WPAR to the gather model, rejects other
  destinations and requires configuration before gather stores. BNE reads zero for
  synchronous completed bus transfers; a partial burst is not flushed by a register read.
  The exact hardware timing remains outside this implementation.

PE EFB queries, bounding-box/performance reads, PI FIFO reset, graphics-command
processing and presentation remain unsupported. No fallback fabricates draw completion.
Aurora's raw processor cannot be connected blindly: it requires complete commands,
its CP array-base handling expects host pointers, and its nested display-list path
currently skips calls. A Kirby adapter must retain partial commands across bursts,
resolve and bound guest arrays/display lists/textures, and connect a properly initialized
Aurora renderer. Those are the next concrete integration points.

### Verification

Final executable: Linux x86-64 ELF, build ID
`e8a86d7473707b3643e903936cec28b9b0cc2498`.
All **26 Python/native tests** and **15 CTest suites** pass. The new GX suite covers
PE acknowledgments, masks, FIFO wrap, partial bursts, ordering, disabled readers,
watermark/breakpoint interrupts, missing consumers, overflow and invalid-pointer rejection,
WPAR and the actual gather exports. Interrupt-controller tests additionally verify CP/PE
signals through the native MMIO routes and independent PI acknowledgments.

Two final bounded native startup runs exit **1**, with no SPR 921 warning, at:

```text
[kirby] blocked: GX FIFO has queued commands but no Kirby command consumer is connected
[kirby] pc=0x80004050 lr=0x80684ea8 ctr=0x0 r1=0x809e4200
[kirby] GX FIFO: control=0x15 base=0x9e4420 end=0xaa4400 queued=32 bursts=1 consumed=0
```

The original SDK selected the physical ring `0x009E4420..0x00AA4400`, inclusive in
32-byte blocks, giving size `0xC0000` (768 KiB). Control `0x15` enables reading,
overflow interrupts and the CPU/GPU link. The first burst is retained in guest RAM;
zero bursts have been consumed. Normal startup still has zero DATA file reads, and
no game frame, menu, usable controller or Fountain Gardens milestone has been observed.
This is an internal graphics-integration dependency, not a missing user input or asset.

Commands executed from the repository root:

```bash
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 --target \
  kirby_gx_fifo_test kirby_interrupts_test kirby_exi_dma_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
readelf -n build/native/WiiCompiled
nm -C build/native/WiiCompiled
```

Evidence is retained locally in `build-logs/stage12-build.log`, `compile.log`,
`stage12-test-build.log`, `stage12-ctest.log`, `stage12-native-tests.log`,
`stage12-startup.log` (initial PE stop), `stage12-startup-pe.log` (initial FIFO stop),
`stage12-startup-final.log` and `stage12-startup-final-repeat.log`.

Added files: `runtime/kirby_gx_fifo.h`, `runtime/kirby_gx_fifo.cpp`,
`runtime/kirby_gx_bridge.cpp`, `runtime/wiicompiled-gx-wpar.patch`,
`tests/gx_fifo_test.cpp`. Updated files: `runtime/kirby_interrupts.cpp`,
`runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`, `tests/interrupts_test.cpp`,
`README.md`, `THIRD_PARTY.md` and this report. No new native guest-function overrides
were added. Vendor checkouts remain clean, and all game-derived files remain ignored.


## Stage 13: Aurora register stream and native Vulkan initialization

The FIFO now has a Kirby command consumer. Original RK5E01 `GXInit` completes when
running with `--video`; execution returns to `fn_806404B0` before the first framebuffer
copy. This is a native executable test, with no Dolphin execution. No image has yet
been submitted for presentation, and no title/menu/controller gameplay is claimed.

`GxStream` retains incomplete FIFO packets across 32-byte bursts, frames CP/BP/XF,
indexed XF, invalidation and vertex packets, and expands guest display lists rather
than using Aurora's path that skips nested list calls. The decoder bounds pending data
and display-list expansion to 4 MiB per feed and nesting to 16 levels. Truncated lists,
invalid addresses, unknown opcodes and oversized vertex packets fail explicitly. A
backend failure poisons the stream so already dispatched commands cannot be replayed.
Display-list and vertex framing are verified with synthetic tests; the observed boot
stream has not exercised either display lists or draw packets.

`GxRenderer` sends complete register commands to Aurora's actual decoder. CP array
bases become bounded host views of MEM1/MEM2, with big-endian source data; no guest
address is cast directly to a host pointer. Indexed matrix loads validate their ranges
before reaching Aurora. BP write masks use Aurora's register shadow. Token/finish
writes update the PE model after the register-only work has completed. Actual draw,
TLUT and EFB-copy commands remain explicit stops until their memory adapters exist.
Once GPU draws are enabled, completion signaling must also account for submitted work.

The optional `--video` path initializes Aurora, starts its frame recorder and connects
the same FIFO consumer. It stores configuration path strings for the renderer's entire
lifetime and shuts Aurora down only after successful initialization. The resource path
is the local Kirby directory rather than another product's bundled cache location.
The default `--headless` path can process register-only work but stops before operations
that need an active render target.

### Failures reproduced and corrected

- The first adapter compilation exposed Aurora's private Abseil include dependency.
  Giving the adapter its own object target with explicit Aurora/Abseil/Tracy dependencies
  fixed it without changing generated shard compilation.
- The first headless run crashed in `aurora::gfx::get_render_target_size`, through
  `map_logical_scissor` and `bp_scissor`. A host GDB backtrace reproduced the null render
  recorder access. Headless scissor/viewport operations now stop with a diagnostic.
- Initializing the Vulkan device/window alone still left the recorder inactive. Calling
  `aurora_begin_frame` before guest GX setup resolved the graphical run's same fault.
- Test compilation corrected use of Aurora's matrix/vector accessor API. The standalone
  integration test then exposed circular static-library references; a CMake RESCAN group
  for Aurora GX/VI/core resolved the link without test stubs or vendor source edits.

### Verified result

Final executable build ID: `f093be497bd35c9ede13b599a19efe0b80244b1c`.
All **27 Python/native tests** and **17 CTest suites** pass. New coverage includes
78 framing checks and 12 checks against Aurora's real register state: vertex descriptors,
array pointers/strides, BP masks, direct/indexed matrix loads, PE events and rejected
unsafe paths. The native regression test checks that headless startup reports the
missing render target instead of crashing.

Two final native `--video` runs exit **1** with the same result:

```text
[kirby] Aurora window initialized; backend=4; no game frame has been submitted
[kirby] blocked: GX display copy needs its guest-memory adapter: destination=0x10096860 control=0x4803 source=640x480
[kirby] GX stream: packets=282 register writes=281 display lists=0 pending bytes=32
[kirby] pc=0x80004050 lr=0x806405e8 ctr=0x80684a30 r1=0x809e4220
[kirby] GX FIFO: control=0x15 base=0x9e4420 end=0xaa4400 queued=32 bursts=52 consumed=51
```

There are 51 accepted FIFO bursts (1,632 bytes); the current burst stays queued after
the copy is rejected. Packet counts include a non-register command. The source is 640x480 and the copy
destination is physical MEM2 `0x10096860`. Control `0x4803` requests a display copy with
clear. The saved LR is outside `GXInit`, demonstrating return to the game's caller.
Normal boot still has zero DATA file reads at this point. The headless run stops earlier,
at scissor setup after 91 packets / 90 register writes.

The actual desktop reports **Acer Nitro AN515-55**, with Aurora selecting **NVIDIA
GeForce RTX 3050 Laptop GPU**, Vulkan, driver `615.78.08`. Its initial Aurora framebuffer
is 640x528; this is a backend default, not proof of matching Kirby's requested output.
The Lenovo Legion Go remains the target device; no Legion Go display or controller test
has been performed in this stage.

The next concrete work is to adapt that MEM2 display-copy destination, preserve required
CPU visibility of framebuffer data, and connect the resulting XFB to Kirby's VI selection
and presentation. A blank development window is not a game-frame milestone. General
interrupt dispatch, actual controller delivery and further IOS/audio services remain
unfinished; no objective-complete claim is made.

### Reproduction

The container build and tests remain unchanged except for the two new targets:

```bash
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 --target \
  WiiCompiled kirby_gx_stream_test kirby_gx_renderer_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh timeout 15s build/native/WiiCompiled --headless
```

For the graphical test, run on the host desktop, not inside the unmodified build
container. `ldd` found a single bzip2 soname mismatch on this host; the installed library
was exposed through an ignored local symlink, leaving the host installation unchanged:

```bash
mkdir -p local/host-libs
# Run once; the link already exists in this workspace.
ln -s /usr/lib64/libbz2.so.1 local/host-libs/libbz2.so.1.0
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout 20s build/native/WiiCompiled --video
```

The GDB reproduction used the same local library path and
`gdb -batch -ex 'handle SIGSEGV stop print nopass' -ex run -ex 'bt 12' --args
build/native/WiiCompiled --headless`. The crash is fixed in the final build.
Evidence remains ignored under `build-logs/stage13-gdb.log`, `stage13-video-first.log`,
`stage13-video-frame.log`, `stage13-final-build.log`, `stage13-final-ctest.log`,
`stage13-final-native-tests.log`, `stage13-headless-final.log`,
`stage13-video-final.log` and `stage13-video-final-repeat.log`.

Added files: `runtime/kirby_gx_stream.h`, `runtime/kirby_gx_stream.cpp`,
`runtime/kirby_gx_renderer.h`, `runtime/kirby_gx_renderer.cpp`,
`tests/gx_stream_test.cpp`, `tests/gx_renderer_test.cpp`.
Updated files: `runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`,
`tests/test_native_bootstrap.py`, `README.md`, `THIRD_PARTY.md` and this report.
Vendor checkouts remain clean; no native guest-function override, translation change,
launcher or packaging step was added. Game files, generated code and binaries remain local.

## Stage 14: GPU display-copy readback and VI selection

The first RK5E01 display copy now completes in the native executable. Aurora resolves
the EFB to a GPU texture, its existing readback service packs native GX RGBA8 tiles,
and Kirby's adapter converts those pixels to linear YUYV and writes the checked guest
memory range. It does not fabricate a completed copy or fill the destination with a
constant color. Separate synthetic GPU tests distinguish black, red and green copies.

Observed on the same Acer/NVIDIA Vulkan host as stage 13:

```text
[kirby] XFB GPU readback: destination=0x10096860 size=640x480 bytes=614400; awaiting VI selection
```

The cached alias is `0x90096860`. GDB read its first 16 bytes as repeated
`10 80 10 80` (black YUYV). This is a cleared startup framebuffer, not a game image.
The original graphics initializer proceeds beyond the former display-copy exception.
The native executable then waits indefinitely in this guest call chain:

```text
SelectThread             0x8066B220
OSSleepThread            0x8066BF70
fn_8067A000              0x8067A000  (video-retrace wait, identified from its body)
fn_80641178              0x80641178
fn_806404B0              0x806404B0
fn_80640414              0x80640414
gfl::Init                0x8063ADA4
main                     0x80647640
__start                  0x80004050
```

Three repeated debugger runs and the final-build debugger run reached the same wait.
The translated body at `0x8067A000` sleeps on the queue at `r13 - 4760` and waits for
the counter at `r13 - 4724` (`0x808E616C`) to change. The idle loop at
`0x8066B370` repeatedly reads the run-queue bits at `r13 - 5280` (`0x808E5F40`).
Both words were zero in the final debugger capture. Swiftshine's `OSThread.c` confirms
that this loop enables interrupts and waits for a runnable thread. The runtime does
not yet deliver general external interrupts to the translated CPU; device time also
advances at MMIO boundaries, which this idle loop does not cross. Returning early
from the retrace wait would conceal both missing behaviors and is not implemented.

The next boot task is to provide a full-context interrupt/scheduler boundary, advance
VI while the guest idles, and deliver its original interrupt handler so the original
thread wakeup and deferred VI register updates can run. The RK5E01 handler/context
addresses and the translator's resident-register contract must be audited before
adding this boundary. No new guest-function override or translated-code edit was
introduced in this stage.

### Implementation and verification limits

- `kirby_gx_xfb.cpp` submits a GPU copy/readback without presenting. The checked RAM
  write completes before FIFO consumption resumes. A readback has a five-second
  completion deadline. Up to 16 RAM destinations retain separate GPU snapshots.
- `kirby_xfb.{h,cpp}` validates the display-copy registers and converts GX 4x4 AR/GB
  tiles to Y0/U/Y1/V pairs. Tests cover tile/row boundaries, adjacent-pixel chroma,
  malformed layouts, truncated results and memory-space overflow. The coefficients
  follow Dolphin; exact hardware rounding is not claimed.
- `VideoSi::Output` decodes top/bottom framebuffer addresses, page-offset mode,
  picture width/stride and active height. Field changes notify the renderer. A matching
  progressive layout or an interlaced field pair can select a retained copy. CPU changes
  to the RAM snapshot are detected and rejected until a RAM-to-GPU upload path exists.
- The synthetic GPU probe verifies all bytes of three 32x4 black/red/green copies,
  surrounding RAM sentinels, selection of an older XFB, duplicate suppression,
  CPU-write detection and rejection of an unmapped destination. Its colored window
  is a test fixture, not evidence of a Kirby frame.
- Current readback seals a graphics recording and starts another. Aurora starts the
  next EFB with clear values. The adapter therefore accepts only the initial clear
  copy path with color/depth writes enabled; non-clear copies, partial write masks,
  gamma/field/half-scale copies, vertical scaling and padded output strides stop
  explicitly. Preserving EFB attachments across arbitrary flushes remains required
  before general rendering. Draws and TLUT loads still stop at their existing guards.
- Bring-up marks pipelines as required so a clear is not silently skipped during
  compilation. VI presentation has only synthetic validation. The real game has
  neither selected/presented an XFB nor reached its title/menu. Controller input,
  audio playback and Legion Go validation remain pending. Existing SI tests still
  model four disconnected GameCube ports; Wii Remote input needs IOS/Bluetooth.

An early native run and the first isolated cold-cache graphics run raised SIGSEGV.
The cold-cache probe reproduced the symptom outside the game. Five cold-cache debugger
runs and eight runs with a local crash-capture shim then passed without reproducing
it. The final binary passed three additional cold-cache runs without that shim.
There is no captured faulting host stack or proven cause, so the intermittent crash
remains open; requiring pipelines is a correctness change, not a demonstrated fix
for that crash. Evidence is retained in `stage14-video-first.log`,
`stage14-gpu-cold.log`, `stage14-gpu-cold-gdb.log`, `stage14-gpu-debug-*.log` and
`stage14-crash-probe-*.log`. The diagnostic shim and disposable caches remain ignored
under `local/` and are not needed to build or run the project.

### Commands and results

```bash
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 --target \
  WiiCompiled kirby_xfb_test kirby_video_si_test kirby_gx_renderer_test \
  kirby_interrupts_test kirby_exi_dma_test kirby_ave_gpio_test
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 15s \
  build/native/kirby_gx_renderer_test --gpu-xfb
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 10s stdbuf -oL -eL \
  build/native/WiiCompiled --video
```

The initial link attempt exposed that this runtime does not export the public
`VISetBlack` symbol under that name. Calling Aurora's existing internal
`video::set_black` resolved the link without changing vendor sources. Final results:
18/18 CTest cases and 27/27 Python/native integration tests passed. Each of three
fresh-cache GPU runs passed 201 checks. The final ELF Build ID is
`cac5a76fc90aa4280aa728afbc6fa391b9ff61fb`.
The ordinary startup is bounded with `timeout -k` because the guest idle loop does
not process window exit events; termination status is not a successful boot.

To capture the waiting stack and RAM bytes reproducibly on the host:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  timeout --preserve-status -s INT -k 5s 8s gdb -q -batch \
  -ex 'set pagination off' -ex run -ex 'bt 12' \
  -ex 'x/16bx 0x100090096860' -ex 'x/4bx 0x1000808E616C' \
  -ex 'x/4bx 0x1000808E5F40' --args build/native/WiiCompiled --video
```

These host addresses include WiiCompiled's x86-64 fixed flat guest base
`0x100000000000`; they are not new guest-memory assumptions. The debugger exits
successfully after the deliberate interrupt, but the guest is still blocked.

A fresh-cache GPU reproduction can use an ignored temporary working directory:

```bash
repo_dir="$PWD"
probe_dir="$(mktemp -d "$repo_dir/local/xfb-probe-XXXXXX")"
(cd "$probe_dir" && LD_LIBRARY_PATH="$repo_dir/local/host-libs" \
  timeout -k 3s 15s "$repo_dir/build/native/kirby_gx_renderer_test" --gpu-xfb)
```

Final evidence is in ignored `build-logs/stage14-final-build.log`,
`stage14-final-test-build.log`, `stage14-final-ctest.log`,
`stage14-final-native-tests.log`, `stage14-final-native-gdb.log` and
`stage14-final-gpu-cold-{0,1,2}.log`.

Added files: `runtime/kirby_xfb.h`, `runtime/kirby_xfb.cpp`,
`runtime/kirby_gx_xfb.cpp`, `tests/xfb_test.cpp`.
Updated files: `runtime/kirby_gx_renderer.{h,cpp}`, `runtime/kirby_video_si.{h,cpp}`,
`runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`, `tests/gx_renderer_test.cpp`,
`tests/video_si_test.cpp`, `README.md`, `THIRD_PARTY.md` and this report.
Vendor checkouts remain clean. Game data, generated C++, executables, debugger logs
and graphics caches stay local and outside Git.

## Stage 15: original retrace IRQ, scheduler wakeup and STM dimming completion

The native boot now passes its three initial video-retrace waits. Device time advances
while the original thread is idle, the original SDK interrupt dispatcher invokes the
original VI handler, and that handler wakes the thread through the SDK run queue.
Startup proceeds into `WPADInit` (`0x806B4360`) and `WUDInit` (`0x806C5F60`).
The next reproducible failure is an IOS device dependency:

```text
[kirby] blocked: Unimplemented IOS open: /dev/usb/oh1/57e/305
[kirby] GX stream: packets=285 register writes=284 display lists=0 pending bytes=2 XFB copies=1 VI submissions=0
[kirby] scheduler: IRQ returns=7 resumed waits=3
[kirby] SC: items=11 IOS callbacks=14 KD RTC synchronized=1
```

Two ordinary graphical runs reached this same failure, followed by a debugger capture.
GDB confirmed the call chain from WPAD/WUD through `fn_806F6720` and the original
`IOS_Open` wrapper into the native IOS transport. The guest retrace counter
`0x808E616C` was 3; `StmVdInUse` at `0x808E5F64` was zero, confirming that the original
STM dimming completion released its request. The real game still has zero VI
presentation submissions and zero normal-startup disc reads. No game frame, title,
menu, Fountain Gardens or working controller is claimed.

### Audited boundaries

`tools/scheduler_audit.py` verifies function addresses, sizes and SHA-256 fingerprints
against the local verified DOL. It also checks the scheduler globals against community
symbols and confirms their r13 references in `SelectThread`. `tools/kirby.py prepare`
emits the resulting constants into the ignored `KirbyBootConfig.h`.

The new native boundaries are `SelectThread` (`0x8066B220`, size `0x22C`) and
`OSLoadContext` (`0x80664450`, size `0xD8`). The IRQ path calls the original dispatcher
at `0x80668650` and VI handler at `0x80678EE0`. `OSSaveContext`, `OSSetCurrentContext`,
`OSClearContext`, SDK thread initialization, VI initialization and VI waits remain
translated code. Four native overrides are now active; the build contains 32,733
translated functions across the existing 72 shards. All foreign registrations
remain rejected.

The scheduler follows the original queue/priority rules and retains a complete host
CPU snapshot at its call boundary. During idle it enables IRQ delivery, advances the
devices through the existing MMIO synchronization path and invokes the original
interrupt dispatcher. `OSLoadContext` unwinds that dispatcher to the matching saved
context rather than returning into code that follows a PowerPC `rfi`. When the SDK
selects the same waiting thread, its existing native call stack resumes. Interrupt
masking, scheduler-disable state, thread state and queue membership are respected.

This remains an incomplete scheduler: selecting a different guest thread, restoring
an unrelated context or scheduling an exception-marked thread stops explicitly.
There is no arbitrary host thread/context creation or instruction-level preemption.
Idle waits have a five-second diagnostic deadline. These limits must be extended as
startup reaches additional threads; the existing Wario host-context/fiber interfaces
are a reference for that work. No wait is replaced with an unconditional success and
no runnable thread is fabricated.

The first isolated test found an ABI declaration issue: the native registration named
its `CpuContext` parameter `ctx`, while the function definition named it `cpu`.
WiiCompiled's effect scanner uses the registration parameter name when inspecting the
body. It consequently omitted the argument-register synchronization before
`OSLoadContext`. Matching the names and retranslating fixed this; the generated call
now flushes r3. Vendor code was not modified. The new native probe is a regression
check for the actual translated call path, not just a direct C++ call to the hook.

### STM dependency encountered by the VI handler

The original handler issued STM ioctl `0x5001` from `__OSSetVIForceDimming`
(`fn_8066CD10`). The IOS adapter now accepts its verified 32-byte no-dimming request:
control zero, reserved zero words and the SDK's register masks. Native output already
uses full brightness. The service retains that setting and completes through the
original asynchronous transport/callback; it leaves output bytes untouched, matching
the reviewed Dolphin STM implementation. Active dimming controls still fail explicitly
until a brightness adapter exists. The unit tests check the successful request,
unchanged output, truncated inputs, unexpected masks and unsupported active dimming.

### Verification and reproduction

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 --target \
  WiiCompiled kirby_ios_service_test kirby_ios_nand_test kirby_disc_test kirby_es_metadata_test
bash tools/container.sh build/native/WiiCompiled --probe-video-retrace
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 15s stdbuf -oL -eL \
  build/native/WiiCompiled --video
```

Final validation: 18/18 CTest cases and 28/28 Python/native tests passed. The new
headless retrace probe calls the original interrupt, thread and VI initializers,
verifies installation of the expected handler, tests EE/PI masking, and resumes three
original VI waits. It checks the stack, r2/r13, all nonvolatile GPRs/FPRs, GQR1-7,
interrupt-enable state, current thread/context and run-queue consistency after every
wait. The probe exits 0. Graphical startup exits 1 at the Bluetooth device open.
Final ELF Build ID: `837064273dbca95da50c5c8d3a43f08500947fc0`.

The debugger capture used:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 15s gdb -q -batch \
  -ex 'set pagination off' -ex 'catch throw std::runtime_error' -ex run -ex 'bt 16' \
  -ex 'x/4bx 0x1000808E5F64' -ex 'x/4bx 0x1000808E616C' \
  --args build/native/WiiCompiled --video
```

The x86-64 host memory addresses include the existing fixed guest-memory base.
Evidence remains ignored under `build-logs/stage15-translate2.log`,
`stage15-final-build.log`, `stage15-retrace-probe2.log`, `stage15-final-ctest.log`,
`stage15-final-native-tests.log`, `stage15-video-second.log`,
`stage15-video-final.log` and `stage15-video-gdb.log`. Early failure logs record the
argument-sync issue and the previously unsupported STM request. The intermittent
cold-cache graphics failure from stage 14 remains unresolved; this stage did not
reproduce it or establish its cause.

Added: `runtime/kirby_scheduler.h`, `runtime/native-src/kirby_scheduler.cpp`,
`tools/scheduler_audit.py`.
Updated: `tools/kirby.py`, `runtime/kirby_main.cpp`, `runtime/kirby_targets.cmake`,
`runtime/kirby_ios_service.{h,cpp}`, `runtime/kirby_interrupts.h` (description),
`tests/ios_service_test.cpp`, `tests/test_native_bootstrap.py`,
`runtime/native-src/README.md`, `README.md`, `THIRD_PARTY.md` and this report.
The DOL, translated C++, profiles with local paths, binaries and logs remain ignored;
upstream/vendor source and license files remain unchanged.

The next concrete task is to audit Wario's reusable `hle/input/wpad.cpp` and `kpad.cpp`
against Kirby's WPAD/KPAD functions and data layouts, then connect a native controller
backend at a verified boundary. The alternative lower boundary is an owned IOS USB/HCI
service for the original Bluetooth stack. Merely returning a successful device handle
without either implementation would hide the current failure and is not implemented.

## Stage 16 — native WPAD bridge and first BPE thread switch

Normal native startup now passes the previous Bluetooth device-open blocker. Fifteen
audited WPAD overrides use Aurora's existing native input libraries. Kirby's original
higher-level input processing remains translated. Startup reaches
`gfl::file::FileSystemWii::StartBPEThread` (`0x8064186C`), resumes its new worker and stops
at the missing host continuation for that worker. No game frame, title, menu, physical
controller input or Fountain Gardens milestone has been observed.

### Implementation and evidence

`tools/wpad_audit.py` checks the community function names, addresses, sizes and SHA-256
digests against the already verified RK5E01 DOL. It also checks the original
`WPADSetCallbackByKPAD` test callback and its `_wpadUsedCallback` object. Preparation
exports the audited addresses in the ignored `generated/KirbyBootConfig.h`.
The runtime registration gate and startup verification check every native address/name
pair. Translation reports 19 native contracts, 16 precise and 3 requiring the full CPU
context; 32,718 translated functions remain linked in the existing 72 shards.

The native input boundary implements initialization/status, probe/read, data format,
connection/extension/sampling callback registration, sampling buffers, gravity scale,
rumble, disconnect, DPD sensitivity and MotionPlus status for a core Remote. It links
Aurora's unchanged WPAD and PAD libraries, which obtain host input through SDL. There
is no fabricated successful IOS USB handle. Guest pointers and callbacks never become
host pointers. Core reports are serialized field by field to 42-byte big-endian guest
buffers after a complete RAM-boundary check. The sampling ring retains its guest
address, size and index, and queued events retain the sampled report.

Aurora callbacks enqueue events without entering translated code. Delivery occurs after
the host event loop at `OSRestoreInterrupts` or scheduler idle, with guest EE enabled.
Guest callbacks run with EE masked on a copied CPU context; the interrupted caller is
preserved. Polling is limited to one update per five milliseconds at those boundaries.
This does not provide general timer-driven input interrupts during arbitrary translated
loops. Extension formats above core format 2 stop explicitly. Additional WPAD commands,
physical device mapping, sideways Remote orientation, controller hotplug in gameplay,
IR controls and game-level rumble behavior remain unverified or unfinished. Aurora's
default core button mapping is still in use; this is not a claim of playable controls.

Wario's WPAD/KPAD native implementations were reviewed. Their game-specific addresses,
KPAD output structures and gameplay patches were not reused. Keeping Kirby's higher
input layer translated avoids assuming that Wario's KPAD layout applies to this game.
Aurora and Swiftshine/key licenses and attribution remain intact in the vendor trees;
new bridge code is GPL-3.0-only.

### Verification

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh python3 tools/kirby.py prepare
bash tools/container.sh cmake --build build/native --parallel 4 --target WiiCompiled
bash tools/container.sh build/native/WiiCompiled --probe-wpad
bash tools/container.sh ctest --test-dir build/native -R '^kirby_' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 20s stdbuf -oL -eL \
  build/native/WiiCompiled --video
```

Final verification: **18/18 CTest cases and 29/29 Python/native tests passed**.
The new `--probe-wpad` invokes the registered native functions through the guest ABI.
It injects reports through Aurora's PAD test interface and checks disconnected/connected
device results, big-endian buttons, output sentinels, sampling-ring initialization and
wrap order, gravity scale, disconnect clearing, invalid buffers and unsupported formats.
It invokes the original translated `WPADSetCallbackByKPAD` as a sampling callback and
checks EE masking and preservation of the caller's GPRs, FPRs, MSR, LR, CR and XER.
This tests the native-to-guest bridge, not physical SDL input. The initial test attempt
incorrectly used `PADSetVirtualStatus` with a disconnected error; Aurora treats any
virtual report as a connected test source. The test now clears virtual sources to
exercise absence before injecting a connected report. No production fallback was added.

Final ELF Build ID: `ac987842c55d8acf580e1ad76aadbf6a5bc20a22`.
Graphical startup exits 1 without reaching the timeout, consistently in normal and
debugger runs. The stop is:

```text
WPAD initialized with Aurora native controller backend
scheduler switch requires host context:
  old=0x808fe0f8 next=0x808fd798 entry=0x8064214c
blocked: Scheduler selected another thread; its host continuation is not registered
```

The debugger command was:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 20s gdb -q -batch \
  -ex 'set pagination off' -ex 'catch throw std::runtime_error' -ex run \
  -ex 'bt 16' -ex 'x/8bx 0x1000808FD930' -ex 'x/16bx 0x1000808FDA60' \
  --args build/native/WiiCompiled --video
```

The host stack identifies `OSResumeThread` (`0x8066BB40`) called from
`StartBPEThread` (`0x8064186C`) through the file-system initializer. Guest memory confirms
the worker's SRR0 `0x8064214C`, SRR1 `0x9032`, ready state 1, detached flag 1,
suspend count 0 and priority/base priority 15. The host addresses in the command include
the existing `0x100000000000` guest-memory base. The worker has been selected but has
not executed its entry. The SDK has seven completed interrupt returns and three resumed
VI waits. Graphics remain at one XFB copy and zero VI submissions; disc data reads remain
zero. This is a scheduler blocker after input initialization, not a new IOS failure.

Ignored evidence: `build-logs/stage16-translate.log`, `stage16-prepare.log`,
`stage16-build.log`, `stage16-final-build.log`, `stage16-wpad-probe2.log`,
`stage16-final-ctest.log`, `stage16-final-native-tests.log`, `stage16-video-first.log`,
`stage16-video-gdb.log` and `stage16-video-final.log`. The final Python/native suite
includes the latest sampling-snapshot and ring-wrap checks. The earlier probe log
predates those additional assertions. The intermittent graphics failure from stage 14
did not recur here; its cause remains unproven.

Added: `runtime/kirby_wpad.h`, `runtime/native-src/kirby_wpad.cpp`,
`tools/wpad_audit.py`. Updated: `tools/kirby.py`, `runtime/kirby_targets.cmake`,
`runtime/kirby_main.cpp`, `runtime/native-src/kirby_ios.cpp`,
`runtime/native-src/kirby_scheduler.cpp`, `tests/test_native_bootstrap.py`,
`runtime/native-src/README.md`, `README.md`, `THIRD_PARTY.md` and this report.
Game data, generated C++, local profiles, binaries and logs remain outside Git.

The next concrete task is to add host stack continuations for the original SDK thread
queues, beginning with the BPE worker and its return to the main thread. The existing
same-thread idle/retrace path must remain valid. Worker startup, sleep/wake, argument
and CPU-state preservation, cancellation and thread exit need verification before
continuing file loading. Wario's host-context support is a reusable reference; all
Kirby context offsets and entry/exit semantics must remain tied to the audited SDK.


## Stage 17 — source layout and additional references

The repository directory is now `Desktop/kirby`, as requested, and VS Code was opened
at the new path. Sixty-five existing source/build/test/audit files were moved into
functional directories. All destinations were checked; none of the old paths remain.
This changes organization and build paths, not game behavior.

- Runtime headers: `runtime/include/kirby/{boot,devices,graphics,services,input,threading}`.
- Implementations: `runtime/src/{app,boot,devices,graphics,services}`.
- Audited native overrides: `runtime/native-src/{services,input,threading}`.
- CMake and upstream patches: `cmake/` and `patches/wiicompiled/`.
- Revision audits: `tools/audits/`.
- Tests: `tests/unit/{boot,devices,graphics,services}`, `tests/integration/`, `tests/tooling/`.

Public includes use the `kirby/<module>/<name>.h` namespace. CMake and tooling refer
to the new paths; the translator still indexes only `runtime/native-src/`, recursively.
The existing snapshot patch contents remain unchanged. Four compatibility header copies
retain their names inside the ignored upstream snapshot for those patches. The
`kirby_tests` build target now assembles all eighteen project unit-test executables.
No vendor source, license or game data was renamed or edited. All game data, translations,
local snapshots, logs and binaries remain ignored. No new external code was imported.

The three supplied repositories were reviewed and pinned in
[references.md](references.md). ResortCompiled is the closest reference for the current
threading work. Its Linux host-context boundary has the same stack-switching operations
already exposed by the pinned WiiCompiled runtime. That existing backend's standalone
test was built and passed; it switches a worker twice and checks continuation positions
and context identity. This does not yet implement the guest BPE thread integration.
WiindowsPlay and NWiiRecomp remain architecture references under the license/reuse
constraints documented there.

Commands run from the renamed repository:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target kirby_tests mkw_linux_host_context_tests
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 20s stdbuf -oL -eL \
  build/native/WiiCompiled --video
```

Translation was run immediately before the repository-directory rename; the container
mount remains `/workspace` at either location. The subsequent build, tests and graphical
run used the renamed directory. Reproduction uses the same commands above throughout.
The native registration count remains 19, with 32,718 translated functions linked in
72 shards. Final ELF Build ID: `343de088ff1fe7843458ed7e7ed716d86ed99448`.
Verification passed: 18 project CTest cases, 1 upstream Linux host-context case and all
29 Python/native tests. The preliminary Python run without `KIRBY_NATIVE_TEST` executed
10 tooling tests and skipped 19 integration tests; the final run enabled all 29.

Native graphical startup still exits 1 at the verified BPE worker switch:
`old=0x808FE0F8`, `next=0x808FD798`, `entry=0x8064214C`. It retains seven IRQ returns,
three resumed waits, one XFB copy and zero VI submissions. There is still no first game
image. The next functional change is to associate native host stacks with the original
SDK thread queues, while preserving the active CPU-context pointer and floating-point
state across switches. Additional graphics commands remain unfinished after that.

Ignored evidence: `build-logs/stage17-layout-translate.log`, `stage17-layout-build.log`,
`stage17-layout-test-build.log`, `stage17-layout-ctest.log`,
`stage17-layout-native-tests.log`, `stage17-layout-video.log` and the local source-move
manifest. Current file paths and responsibilities are in `runtime/README.md`; historical
stage reports preserve their original paths. `README.md`, `THIRD_PARTY.md`, the native
registration guide and this report were updated, and `docs/references.md` was added.

## Stage 18: native thread continuations and stopped-stream AI initialization

The original BPE worker now executes on a native stack through the pinned WiiCompiled
HostContext/libco backend. The original SDK still owns its run/wait queues and thread
lifecycle. The adapter preserves the active CpuContext scope pointer and host FP control
across switches. New stacks use the audited OSCreateThread layout; replaced suspended
contexts are rejected instead of reusing stale host continuations. Worker exceptions
return to the root stack for normal diagnostics. No additional native overrides were
introduced: 19 overrides and 32,718 linked translated functions remain.

`--probe-threads` exercises original SDK creation, sleep, wake, exit, return values and
recreation at the same thread address, with eight native stack switches. It checks SDK
queues, nonvolatile registers, GQRs, CPU scope and FP control. `--probe-thread-fault`
verifies that an invalid WPAD output pointer in a worker returns exit 1 with the expected
diagnostic rather than a signal. This covers cooperative continuations, not arbitrary
PC restoration, preemption or every cancellation path.

The first graphical run reached an unsupported AI volume write at `0xCD006C04` after
two BPE stack switches. The device model now retains volume/threshold registers and
handles the stopped-counter reset strobe used by the original AIInit. Register semantics
were checked against the local SDK AI sources and the existing pinned Dolphin reference.
Streaming playback and rate changes still fail explicitly; no audio mixing was added.

The final graphical run exits 1 at a reproducible next blocker:

```text
guest memory access 0xcc005002 (+0x2): MMIO write blocked (non-GPU)
GX stream: packets=285 register writes=284 display lists=0 pending bytes=2
XFB copies=1 VI submissions=0
scheduler: IRQ returns=7 resumed waits=4 native stack switches=2
```

A host GDB breakpoint at `Kirby::DspBoot::Write16` confirmed low mailbox word `0xA001`.
The native call chain is `func_80684770` -> `func_80684010` -> `func_8067FAE0` ->
`func_8067FBD0` -> `func_8067DE50` -> `func_8064C5E8` (callee to caller). This is the
DSP task loader's first IRAM-address command, `0x80F3A001`, defined in the SDK's
`dsp_task.c`. Supporting it requires the task upload protocol and the verified audio
microcode's behavior; acknowledging the mailbox alone would not provide those services.
That is the next concrete implementation boundary. Draw/texture adapters remain incomplete
after boot. No game image, title, menu or playable controller milestone was observed.

One preliminary graphical run also hit the previously observed intermittent signal 11
at address `0x108`. Subsequent normal and GDB runs reached the mailbox blocker. The
intermittent graphics crash remains unresolved; passing runs do not establish a fix.

Commands run from the repository root:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target WiiCompiled kirby_tests mkw_linux_host_context_tests
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 20s stdbuf -oL -eL \
  build/native/WiiCompiled --video
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -k 3s 30s gdb -q -batch \
  -ex 'break Kirby::DspBoot::Write16(unsigned int, unsigned short) if $esi == 0xcc005002' \
  -ex run -ex 'p/x $edx' -ex 'bt 12' --args build/native/WiiCompiled --video
```

Verification passed: 18 project CTest cases, the upstream Linux host-context case and
all 31 Python/native checks. ELF Build ID: `63ee299a3b48c3830527dc26c806e48c8c2fb9f8`.
Ignored evidence is in `build-logs/stage18-*`, including `stage18-video-final.log`,
`stage18-dsp-mail-gdb-final.log`, `stage18-ctest.log` and `stage18-native-tests.log`.

Authored changes: `runtime/{include/kirby,src}/threading/continuations.*`,
`runtime/native-src/threading/scheduler.cpp`, `runtime/src/app/main.cpp`,
`runtime/{include/kirby,src}/devices/interrupts.*`, `tools/audits/scheduler.py`,
`cmake/native_targets.cmake`, `tests/integration/{thread_probe.cpp,test_native_bootstrap.py}`,
`tests/unit/devices/interrupts_test.cpp`, `README.md`, `runtime/README.md` and this report.
No external source was imported; existing WiiCompiled and libco attribution is retained.

## Stage 19: AXWii, alarms, first draws and black VI output

The native executable now reaches `Game::Run`, executes an untextured four-vertex
GX draw and presents alternating game XFBs through VI. The observed window remains
black. A live capture of the 614,400-byte XFB at `0x90000860` contained luma 16 for
all 307,200 pixels. No recognizable game image, title, menu or playable milestone
is claimed. The final executable was left open at the user's request.

The DSP task loader at `0x80684770` now runs its original translated body behind an
audited wrapper. ROM upload commands latch the IRAM address, length, DRAM length,
IRAM destination and entry vector. Only the verified 8,192-byte AXWii image is accepted:
SHA-256 `d6d5952c5e6c55705513d89e58ba12314db23d8758c4acfd2c75b81b7140c0ed`,
HashEctor `0x4CC52064`, initialization vector `0x10`. The existing WiiCompiled AX mixer
is reused through a hardware-only entry that skips its foreign game globals.
The original DSP interrupt callback sets Kirby's initialization flag. The AX probe
checks that path, ordered task mail replies and 96 mixed stereo samples.

The audio DMA registers at `0xCC005030/32/36/3A` now latch and reload bounded RAM
buffers and deliver AID events. A native 32 kHz stereo sink receives the samples.
The host output queue can overflow and drop blocks; audible game audio has not been
validated. Unknown DSP tasks, command layouts and streaming AI rate changes remain
explicit failures.

Startup next stalled with the main thread in `OSSleepTicks`, waiting for an asynchronous
file load. Idle polling now checks the audited SDK alarm queue against system time and
invokes the original `DecrementerExceptionCallback`. The original callback owns queue
updates and thread resumption. Three sleep deadlines, masked delivery, cancellation,
register restoration and the empty final queue pass the native alarm probe.
This is cooperative delivery, not a complete decrementer/preemption implementation.

The next normal run crashed in original `WPADGetInfoAsync` because native input does
not initialize Wii Bluetooth control blocks. An audited bridge at `0x806B5A70` now
uses Aurora's native information request, serializes 24 bytes into guest endianness,
and queues successful callbacks at a guest boundary. Tests cover absent controllers,
immediate failure callbacks, duplicate requests, interrupt masking, buffer canaries
and out-of-range output rejection. This brings native overrides to 21.

The next reproducible graphics boundary was the first draw. `gx_draw.cpp` now submits
direct vertex packets, resolves bounded RAM texture data, and snapshots palette uploads
into a 512 KiB palette TMEM view. The first draw observed has four vertices and no sampled
texture. A later run reached TLUT upload before that support was added. Indexed vertex
uploads and EFB texture copies remain unsupported; the new texture paths do not establish
that the game has displayed a textured frame.

GPU readback had been polling Dawn on the guest thread while Aurora submitted work
on its render worker. GDB observed crashes in NVIDIA command recording/submission
and an Intel/Dawn queue assertion. Polling now runs on the render worker and waits
for that work before checking the readback packet. Dawn validation/robustness remain
enabled in its setup translation unit even in the Release build. Subsequent native
runs sustained thousands of black presentations, and all 201 synthetic Vulkan XFB
checks passed. This does not establish a general driver fix. Heap-corruption diagnostics
at shutdown were still observed on preceding blocked runs and remain unresolved.

After 1,273 copies and 1,267 VI submissions, the game reached FS ioctl 7 (delete).
The local NAND backend now removes regular files or empty directories using bounded
paths and directory descriptors; recursive deletion remains unsupported. Next came
FS ioctlv 12 (directory usage). It now returns actual local file/directory counts and
16 KiB cluster usage, with validated request vectors. Independent directory opens
prevent repeated scans from sharing a cursor. Tests cover per-file rounding, repeated
queries, missing paths, symlink rejection and malformed vectors.

Verification: 19 project CTest cases plus the upstream Linux host-context test passed;
all 33 Python/native checks passed. The synthetic GPU readback test passed separately.
ELF Build ID: `afa3e3db1859faefafa78171d180c7dde59bec77`. The executable links 32,716
shard functions plus the retained original DSP loader body, using 72 shards.
The DOL hash remains `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`.

Commands run from the repository root:

```bash
# Default translator build encountered NU1301. Reuse the already built pinned DLL:
bash tools/container.sh python3 tools/kirby.py translate --threads 4 \
  --translator-dll vendor/wiicompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --parallel 4 \
  --target WiiCompiled kirby_tests mkw_linux_host_context_tests
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --probe-wpad
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --probe-ax-task
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --probe-alarms
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby_gx_renderer_test --gpu-xfb
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --video
```

Evidence remains ignored under `build-logs/stage19-*`. Relevant files include
`stage19-info-translate.log`, `stage19-video-serialized.log`,
`stage19-draw-gdb.log`, `stage19-intel-exit-gdb.log`, `stage19-video-palette.log`,
`stage19-native-palette-window.png`, `stage19-xfb-capture.log`,
`stage19-final-ctest.log`, `stage19-final-native-tests.log` and `stage19-gpu-xfb.log`.
`local/current-xfb.yuyv` is a local guest-buffer capture, not a distributable asset.

Authored changes are grouped under `runtime/{include/kirby,src}/devices` (DSP, AX,
audio DMA), `runtime/native-src/{devices,input,threading}`, `runtime/src/graphics`
(draws, palettes and readback polling), `runtime/src/services` and matching headers
(NAND usage/delete), `tools/audits/{audio,scheduler,wpad}.py`, `tools/kirby.py`,
`tools/build_native.py`, `cmake/native_targets.cmake`, `patches/wiicompiled/ax-hardware.patch`,
matching unit/integration tests, the READMEs, `THIRD_PARTY.md` and this report.
The pinned vendor checkout remains unchanged. Game data and generated code stay ignored.

Next: trace the game's startup task state after NAND checks and capture its first
textured draw. Determine whether the black output is requested by the game or caused
by remaining graphics/state translation before claiming the first visible image.

## Stage 20: textured-draw isolation and EFB persistence

Live debugger captures found real textured UI quads: XY/F32 positions, ST/F32
coordinates, an orthographic projection, texture matrices and two-stage TEV alpha
blending. The full-screen quad samples a one-pixel I4 texture whose pixel is white.
Scissor covers 640x480, culling and fog are disabled, and alpha comparison always
passes. Both alternating XFBs still contained only luma 16 before the fix below.
Captured game packets and texture bytes remain ignored under `local/`.

The Vulkan test now verifies actual red primitives, blue CI4/RGB565 texturing,
XY-only vertices, texture matrices, two-stage TEV and the observed UI projection
with a synthetic one-pixel I4 texture. These pass without a presentation between
Draw and CopyDisplay. Adding VI presentation of an older XFB between those calls
fails the white-pixel assertion (`stage20-gpu-ui-retrace.log`). This isolates a
host-frame boundary bug independently of the game.

Aurora initializes each new recording pass with attachment clears. Starting a new
host frame after VI presentation therefore erased EFB draws that the game had not
yet copied. A separate submission after CopyDisplay also imposed an extra clear
regardless of the guest's clear flag. `patches/aurora/preserve-efb.patch` adds an
explicit operation to load color/depth attachments on a resumed EFB pass;
`gx_xfb.cpp` calls it after these two boundaries. Initial window initialization
retains its initial clear. Guest-requested copy clears remain recorded normally.
The build uses a patched local Aurora snapshot with its MIT license intact.

Audio DMA also recovers from debugger pauses: it skips obsolete host output beyond
256 blocks (64 ms), advances the guest DMA cursor/reload state arithmetically, and
coalesces the pending AID interrupt. Previously a pause over one second threw and
closed the process. The two-second pause regression verifies cursor, data, IRQ and
reset behavior. This does not establish audible game music.

The corrected GPU probe passes 213 checks (`stage20-gpu-preserved.log`), including
presentation between drawing and copying, a copy with the clear bit unset, and a
subsequent guest-requested green clear. `xfb.cpp` now accepts display copies without
clearing; the old explicit rejection is removed and its layout unit check updated.
This is synthetic GPU evidence; it does not by itself establish a visible game frame.

The current host input inventory (`/proc/bus/input/devices` and `/dev/input/by-id`)
contains no physical gamepad. Controller-report probes remain useful integration
evidence, but physical controller movement and Legion Go validation remain pending.

### Verified native result

Build ID `4a7c646d8417ce1090cce5de52c3b6b31aca3476` renders the real Wii Remote
wrist-strap screen on the NVIDIA Vulkan host. This is the first verified visible
game image. The native process remained running after the capture and debugger
inspection; the later capture showed the game had advanced to the create-save-file prompt. No title, menu or
Fountain Gardens milestone is claimed. The capture is local and ignored:
`build-logs/stage20-first-game-image.png`.

Final verification: 20 CTest cases passed, all 33 Python/native tests passed, and
213 Vulkan checks passed, including a read-only copy with color/alpha/depth writes
disabled. The standalone XFB layout/conversion test passed 28 checks. The build
helper's second invocation also verified that the Aurora patch applies idempotently.
The pinned WiiCompiled and Aurora vendor checkouts remain clean.

Commands used for final build and verification:

```bash
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --target kirby_tests mkw_linux_host_context_tests --parallel 4
bash tools/container.sh ctest --test-dir build/native -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby_gx_renderer_test --gpu-xfb
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --video
```

Changed files in this stage: `patches/aurora/preserve-efb.patch`,
`tools/build_native.py`, `cmake/native_targets.cmake` (renderer debug information),
`runtime/src/graphics/{gx_xfb,xfb}.cpp`,
`tests/unit/graphics/{gx_renderer,xfb}_test.cpp`,
`runtime/{include/kirby/devices/audio_dma.h,src/devices/audio_dma.cpp,src/app/main.cpp}`,
`tests/unit/devices/audio_dma_test.cpp`, `README.md`, `runtime/README.md`,
`THIRD_PARTY.md` and this report. No new native hook or DOL translation was needed.
Additional evidence is in ignored `build-logs/stage20-{final-ctest,final-native-tests,final-gpu,visible-threads,video-preserved}.log`.

Next: verify host input on the create-save-file prompt, exercise save creation and
continue toward the title/main menu. Input probes are verified, but a physical controller and
Legion Go test are still pending. No new fatal boot call was observed in this run.

A later live capture at roughly two minutes reached the create-save-file prompt
(`build-logs/stage20-current-native-window.png`). The wrist-strap screen was a
transient startup milestone, not a reproduced stall. The prompt is not the main menu.

## Stage 21: horizontal controller mapping and indexed arrays

The previous native run detected a hot-plugged USB DualSense. The user reported
rotated D-pad directions and unresponsive action buttons. The old black-window
process was explicitly terminated; a later inspection confirmed it had not exited
on SIGTERM. Only the updated game process is intended to remain open.

Kirby consumes a sideways Wii Remote. The serializer now maps conventional host
D-pad directions to physical remote directions (left->up, right->down, up->right,
down->left). Host south/east/west/north become Wii 2/1/B/A, respectively. On
DualSense this is cross/circle/square/triangle. Plus, minus, home and other bits are
preserved. The native WPAD probe covers all four directions, four face buttons and
serialized sampling-ring delivery. Physical in-game verification remains pending.

The prior game run advanced beyond the save prompt and stopped at the explicit
indexed-array boundary: PC `0x80684064`, LR `0x8002F1C0`, after 13,786 XFB copies
and 13,779 VI submissions. This shows that some input reached the game; it does
not establish that save creation succeeded. No save file was found in local NAND.

`gx_draw.cpp` now scans the actual 8/16-bit indices for each enabled attribute and
uploads only the prefix ending at the highest referenced element. Size arithmetic
uses 64 bits, RAM resolution is bounded, and each attribute upload is limited to
16 MiB. Array bindings are refreshed each draw because guest RAM may change without
CP rebinding. Indexed NBT normals remain an explicit unsupported layout.
The GPU regression renders a textured UI quad with interleaved guest arrays and
mixed index widths, verifies the 56-byte upload extents, then changes positions
without CP rebinding and verifies the new geometry. All 218 Vulkan checks passed.
20 CTest cases and 33 Python/native tests passed before the DPD hooks below.

A second blocker appeared with the controller connected at startup. Two runs
reproduced SIGSEGV at flat address `0x10007C7B1636`, LR `0x806B85EC`, inside
`WPADControlDpd+0x5BC`. The original DPD routine accesses a Bluetooth command queue
that the native WPAD backend does not initialize. Hot-plugging after boot did not
exercise this same initialization path. Logs:
`stage21-video.log` and `stage21-connected-repro.log`.

The new DPD boundaries are verified against Swiftshine symbols and exact DOL bytes:

| Function | Address | Size | SHA-256 |
| --- | --- | --- | --- |
| WPADControlDpd | 0x806B8030 | 0x1080 | a9a68920e3f6fb92683538c4741948ab459180cba4901da13c43a4f27f016b09 |
| WPADIsDpdEnabled | 0x806B7FA0 | 0x44 | 935a8ea9f5c5dc88b28601acf6c2f553117985e6fbde252bd00505fddd84c936 |

Native DPD control uses Aurora pointer enablement and queues original guest
callbacks outside Aurora's noexcept update. Modes 1/3/5 all enable the native
pointer; report format remains the serializer's responsibility. No IR objects
are fabricated when the host has no valid pointer. Unchanged-state and disconnected
requests invoke callbacks synchronously, matching the SDK's immediate completion
path. The probe checks extended enable, deferred callback and disable/query state.

### Final DPD verification

Build ID `0d068618627ba112eff1015d8b86ad41e702ba6a` starts with the USB DualSense
already connected and reaches the visible sideways-Remote notice. It passes the
previous deterministic DPD crash location. The captured native window is
`build-logs/stage21-dpd-window.png`; the live run is logged in
`stage21-video-dpd.log`. The final 33 Python/native tests pass, including the new
DPD enable/query/callback checks. Physical confirmation of the new button/D-pad
mapping and progression beyond the save prompt remains pending.

Commands used:

```bash
bash tools/container.sh python3 tools/kirby.py translate --threads 4 --translator-dll vendor/wiicompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
bash tools/container.sh python3 tools/build_native.py --jobs 4 --translated-jobs 4
bash tools/container.sh cmake --build build/native --target WiiCompiled kirby_tests mkw_linux_host_context_tests --parallel 4
bash tools/container.sh ctest --test-dir build/native -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby_gx_renderer_test --gpu-xfb
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --video
```

The translation updated four functions and left 32,733 unchanged. The manifest now
contains 32,714 shared base functions and 23 native overrides, with the original DSP
loader body linked separately. Vendor checkouts remain clean; game data, generated
code, captures and binaries remain ignored.

Authored files changed in this stage: `runtime/native-src/input/wpad.cpp`,
`runtime/src/graphics/gx_draw.cpp`, `tests/unit/graphics/gx_renderer_test.cpp`,
`tools/audits/wpad.py`, `README.md`, `runtime/{README.md,native-src/README.md}` and
this report. SDK ABI/reference code remains in the pinned Swiftshine checkout;
Aurora's existing MIT input backend is reused without vendor modifications.


### Next observed graphics boundary

The updated run subsequently passed indexed rendering and reached the next explicit
adapter boundary: `GX texture-copy destination adapter is not implemented`.
PC `0x80684064`, LR `0x800EFC44`, caller chain `0x800EFBCC -> 0x8065D56C ->
0x8002DC0C`. At this point it had processed 161,710 display lists, 2,744 XFB copies,
2,739 VI submissions and 392 disc reads (20,665,888 bytes). The process exited;
there is no live native game window after this run. The known heap-corruption
message also recurred during shutdown after the explicit graphics exception.

This replaces the indexed-array stop as the next graphics boundary. Implement a
bounded EFB-to-texture copy/readback adapter using the actual copy format, extent,
stride and destination registers. Preserve guest-visible texture RAM and later
sampling semantics; do not substitute a display copy or skip the operation.
Physical D-pad/action-button verification remains pending despite automated mapping
checks. The native controller detected-at-boot and indexed GPU paths are verified.

## Stage 22: texture-copy readback and NAND creation

The previous goal turn made progress: connected-at-boot WPAD and indexed draws
passed, exposing EFB-to-texture copies as the next graphics boundary. No native
game process was running at the start of this stage.

### GPU copies

`TextureCopy::Decode` decodes the rotated BP copy-format field, intensity/depth
selection, source rectangle, half-scale dimensions and tile-row stride. Source,
RAM destination and arithmetic bounds are checked before GPU submission. Gamma
and field modes remain explicit unsupported modes. The implementation supports
color/channel and depth formats that Aurora's existing copy packer can encode;
this does not claim hardware verification of every format.

`gx_xfb.cpp` shares the serialized readback submission/wait path between XFB and
texture copies. A texture copy uses Aurora's existing conversion path, packs native
Wii tiles, and writes each tile row using the guest's stride, leaving padding
untouched. The destination's GPU copy-cache entry is evicted afterwards: guest RAM
remains authoritative, including subsequent CPU edits. EFB contents remain loaded
when recording resumes. Texture copies do not select a VI framebuffer.

The Vulkan probe passes **261 checks** (`stage22-depth-gpu.log`). New checks cover
RGBA8 AR/GB tile order, row padding, separate tile-row stride, RAM bounds, later
sampling after CPU edits, half-scale color copies and a far-depth Z24X8 copy.
Layout unit tests also cover format rotation, I4, depth selection and invalid modes.
This is synthetic GPU evidence. The latest real save-creation path stops in IOS
before reaching a game texture copy, so that later game operation remains to be
verified. The initial GDB copy capture waited for menu input; it was explicitly
replaced after the updated binary was built, not restarted because a polling
observation expired.

### IOS file creation

Brief virtual controller pulses were sent through the existing `PADSetVirtualStatus`
API, then cleared with `PADClearVirtualStatus`. These pulses use the actual native
input bridge and translated game code. They are not evidence of physical DualSense
operation. The local helper is `local/press-controller.gdb`; game captures, debugger
helpers and logs remain ignored.

Confirming Yes exposed FS ioctl 3, then ioctl 9. The RK5E01 SDK's 76-byte
`FSFileIoctl` places the NUL-terminated path at offset 6, owner/group/other modes
at `0x46..0x48` and the attribute byte at `0x49`. The adapter validates the full
structure and permission ranges. Creation now makes real host directories/files,
rejects existing paths without truncation, does not invent missing parents, and
uses descriptor-relative traversal without following symlinks. Four metadata bytes
persist in the `user.kirby.isfs` extended attribute. Host access remains private;
the existing single-title model does not implement multi-UID Wii ACL enforcement.
An unsupported xattr filesystem returns an error and rolls back the new node.

Tests verify persistent metadata, absent-parent/traversal errors, complete request
sizes, non-truncating creation, actual writes and data surviving IOS close.
A live run initially produced NAND NOEXISTS and a later invalid save-sequence
reference. The local boot filesystem lacked `/tmp`, which IOS creates at startup.
Initializing this directory fixed that path: the latest run logged
`NAND create directory /tmp/sys result=0` and reached the next explicit IOS call.
Temporary directory cleanup across separate boots remains unimplemented; existing
local temporary files are preserved.

### Observed result and next boundary

Build ID **51fda79284bdfd5196a58bc66290f0837cb7acfc** opens the real create-save-file
screen. Confirming Yes now stops at:

```
Unimplemented IOS FS command=6 ioctl=6
pc=0x80684064 lr=0x806A9CD8
XFB copies=3945 VI submissions=3939
```

FS ioctl 6 is `ISFS_GetAttr`. The authoritative log is
`build-logs/stage22-video-tmp.log`. The native process exited after this stop;
no game window remains open. The known shutdown heap-corruption diagnostic still
occurs. Save creation, title/main menu, Fountain Gardens, physical mapping and
Legion Go validation are not established.

Final results: **20 CTest cases**, **33 Python/native tests**, **261 Vulkan checks**
passed. Logs: `stage22-tmp-ctest.log`, `stage22-tmp-native-tests.log`,
`stage22-depth-gpu.log`. Vendor WiiCompiled and Aurora checkouts remain clean.
No native hook or DOL translation changed in this stage.

Commands used:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled kirby_tests --parallel 4
bash tools/container.sh ctest --test-dir build/native -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/kirby_gx_renderer_test --gpu-xfb
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --video
```

To reproduce the current boundary, launch the native binary, wait for the save-file
prompt, and confirm Yes with the mapped Wii 2 button (DualSense cross). The virtual
pulse used a zero-initialized 16-byte native `PADStatus` with button `0x0100` on
port 0; its override was cleared after a brief interval. The first pulse can advance
startup notices; a separate press/release confirms the save prompt.

Authored changes: `runtime/include/kirby/graphics/{xfb,gx_renderer}.h`,
`runtime/src/graphics/{xfb,gx_xfb}.cpp`, `runtime/include/kirby/services/nand.h`,
`runtime/src/services/{nand,ios_service}.cpp`,
`tests/unit/graphics/{xfb,gx_renderer}_test.cpp`,
`tests/unit/services/{nand,ios_nand}_test.cpp`, the project/runtime READMEs,
`THIRD_PARTY.md` and this report. Game and generated code remain outside Git.

Next: implement `ISFS_GetAttr` against actual local metadata, including correct
missing-path behavior, then repeat Yes and inspect the next save operation. Re-run
the real texture-copy path after the save flow progresses; keep the full boot and
controller goal active until those milestones are verified.

## Stage 23: save completion, main menu and Patch Castle

The previous goal turn made progress in texture readback and NAND creation, and
identified FS ioctl 6 (`ISFS_GetAttr`) as the next startup dependency. No game
process was running when this stage began.

### Attribute query and save commit

`NandStore::Attributes` reads the actual `user.kirby.isfs` four-byte metadata record
through a bounded descriptor-relative path. Missing paths return FS_ENOENT without
writing the output. Symlinks, non-file/directory nodes and malformed metadata are
rejected. Unknown host files without an authored metadata record return an explicit
error rather than acquiring fabricated permissions. Boot-owned directories and
synthetic configuration files receive explicit initial records; existing valid
records and configuration bytes are preserved.

The GetAttr wire adapter uses the SDK's 64-byte input path and 76-byte output.
Owner/group/other modes occupy `0x46..0x48`, with the attribute byte at `0x49`.
The private mount executes IOS requests as logical UID/GID 0, so those ownership
fields are zero. Host ownership is not exposed and multi-UID Wii ACLs remain outside
this model. Undefined path/padding output bytes are zeroed, following Dolphin's
deterministic handling. Buffer validation completes before filesystem access/output.

With GetAttr implemented, the game detected the absent FLF.bin, wrote a temporary
43,456-byte file, then stopped at ioctl 8 (`ISFS_Rename`). The rename adapter now
moves files using descriptor-relative `renameat`, preserving bytes and xattrs.
It follows IOS's unchanged-basename rule for files, rejects open sources with
FS_OPENFD (-111), rejects path traversal/symlinks, does not create destination
parents, and replaces existing files. Live file descriptors are tracked by weak
references and inode identity. Replacement of a nonempty destination directory
remains an explicit unsupported recursive-delete boundary.

Tests cover wire sizes, logical ownership, output bounds, nonexistent paths,
corrupt/absent metadata, symlinks, open-file rename rejection, commit bytes,
attribute preservation, replacement and absent destination parents. The existing
settings are preserved while initializing missing metadata, and directory
initialization closes its descriptors if metadata setup throws.

### Observed native milestones

Build ID **b17f76468ad317d1f6d2187203707d7316b40ac0** completed the save-creation
flow and reached the real main menu. The native log records successful commits of
FLF.bin, 30 GF_<slot>_<index>.jpg files, and banner.bin under the mounted title's
DATA directory. The initial completed set contained 32 files totaling 4,037,216
bytes: FLF.bin was 43,456 bytes, each image 131,072 bytes, and banner.bin 61,600 bytes.
All are local/ignored. `local/stage23-save-files.json` records the initial hashes;
subsequent normal gameplay updates FLF.bin, so those initial hashes are not an
immutability assertion.

Verified visible states, in order:

- Main menu with ONE PLAYER/TWO PLAYERS (`stage23-after-save.png`).
- Down selects TWO PLAYERS and up restores ONE PLAYER (`stage23-menu-down.png`,
  `stage23-menu-up.png`).
- ONE PLAYER opens the file selector and File 1 confirmation.
- The opening scene renders; the normal plus/skip confirmation works.
- Kirby and Prince Fluff appear at the castle (`stage23-world.png`).
- The Patch Castle stage card and one-player confirmation open.
- Patch Castle loads with Kirby and the bead HUD (`stage23-castle-level.png`).
- Right movement advances Kirby and the camera (`stage23-castle-right.png`).
- Jumping clears the first step and collects beads, increasing the HUD to 20
  (`stage23-castle-step.png`).
- The Wii 1 action removes the first tutorial block and produces a carried yarn
  object (`stage23-action2.png`).

These input checks used short virtual PAD pulses through the existing native
controller API, followed by `PADClearVirtualStatus`. They exercise the translated
game's actual controls and physics, but do not establish physical DualSense or
Legion Go input. The up/down compensation is now verified in the real menu.
Fountain Gardens has not yet been reached.

The game's first real EFB-to-texture copy also succeeded after skipping the opening:
RGBA8 (format 6), 320x240, physical destination `0x10AE8EA0`, tile-row size/stride
5,120 bytes. This verifies that the Stage 22 adapter executes in the real game, in
addition to its synthetic GPU checks. The retained live log is
`build-logs/stage23-video-rename.log`.

### Validation and remaining work

20 CTest cases passed (`stage23-rename-ctest.log`) and all 33 Python/native tests
passed (`stage23-rename-native-tests.log`). Graphics code did not change; the prior
261-check Vulkan result remains applicable. The latest native run remained live in
Patch Castle for over sixteen minutes without a new fatal boot or graphics call.
The process was not restarted due to an observation timeout. All virtual overrides
were cleared after the movement probes.

The native window remains available for physical controller testing. Audio output
is still not verified audibly; occasional output-queue drops during debugger pauses
are logged. The previously observed shutdown heap corruption, full Wii ACLs,
nonempty directory replacement and temporary-directory cleanup remain unresolved.
Save bytes are verified on disk and by subsequent game operations; an independent
restart/reload verification remains pending.

Commands used:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled kirby_tests --parallel 4
bash tools/container.sh ctest --test-dir build/native -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled python3 -m unittest discover -s tests -v
LD_LIBRARY_PATH="$PWD/local/host-libs" build/native/WiiCompiled --video
```

Local GDB helpers used native PAD button masks: `0x0100` for mapped Wii 2,
`0x1000` for plus, `0x0004`/`0x0008` for host down/up, `0x0102` for right+jump,
and `0x0202` for right+Wii 1. No game progression flags, inventory, actor positions
or original code were patched to produce these milestones.

Changed authored files: `runtime/include/kirby/services/nand.h`,
`runtime/src/services/{nand,ios_service}.cpp`,
`tests/unit/services/ios_nand_test.cpp`, the project/runtime READMEs,
`THIRD_PARTY.md` and this report. No DOL translation, native hook or vendor source
changed. Game data, generated C++, save files, captures and binaries remain ignored.

Next: finish the normal Patch Castle route and reach Fountain Gardens, verify
movement with the physical controller, and independently reload the persisted
save. Keep the full video/SI/boot goal active until those required milestones and
remaining dependencies have authoritative evidence.

## Stage 24: independent save reload and continued Patch Castle traversal

The Stage 23 binary, build ID `b17f76468ad317d1f6d2187203707d7316b40ac0`, was also
started in an independent working directory with a copy of the local NAND/SRAM.
The copy preserved filesystem xattrs. The process used the same native executable
and an absolute extracted DATA path, without sharing the original save mount.

The file selector showed File 1 at 1%, with time 00:01:01. Selecting it loaded
Quilty Square at the Patch Castle doorway without repeating initial save creation.
Captures are `build-logs/stage24-reload-file.png` and
`build-logs/stage24-reload-world.png`; the runtime log is `stage24-reload.log`.
The original FLF.bin SHA-256 remained
`fc034a13879ec25699424fb95758b4e32ee8ef4861083bbe5814a7553dc1767e`
before and after the isolated test (`local/stage24-reload.json`).
This establishes persistence across native restarts for the observed checkpoint.

The original run continued normal Patch Castle traversal to 61 beads, climbed the
first optional chest platform and opened its chest. Inputs used the existing
virtual PAD API. A timed helper stopped the process and cleared each override after
its interval; earlier manually separated press/release calls had imprecise hold
durations. No progression flags, actor coordinates or inventory were patched.
Fountain Gardens was not reached. No frame-rate measurement was made.

The isolated reload process was stopped after its checks. The remaining graphical
process was subsequently closed at the user's request after they reported a black
window, unresponsive controls and reversed DualSense D-pad behavior. It did not exit
on SIGTERM and was terminated with SIGKILL. This is not evidence of clean shutdown.
The latest retained runtime log had no new fatal message; the black-window symptom
has not yet been independently reproduced or explained.

## Stage 25: SDL controller event coverage

The prior WPAD probe injects PAD status, bypassing SDL. The new
`--probe-sdl-controller` diagnostic attaches a virtual SDL gamepad and exercises
SDL's event queue, Aurora's gamepad/source handlers, input router, PAD adapter,
WPAD sample generation and the registered Kirby guest-report serializer.
It checks all four D-pad directions, four face buttons and plus, their releases,
and unplug while a button is held. The test explicitly selects the virtual device
on port 0 and runs in a temporary working directory, preserving the user's input
preferences and save files. No game window or GPU is initialized.

The first diagnostic attempt returned zero PAD buttons despite SDL seeing the
press. Investigation showed that the diagnostic had no Aurora user path; Aurora's
mapping loader returns before installing defaults in that state. Supplying the
same input-configuration prerequisite as the graphical runtime resolved that test
setup failure. No production input mapping was changed. The window event pump's
focus, capture and pause logic is outside this headless diagnostic.

The completed diagnostic passes both on the native host, where the USB DualSense
is enumerated alongside the virtual device, and in the build container. All 34
Python/native integration checks and all 20 selected CTest cases pass. The native
build ID is `711e666ff491bb70b36af31eeb593d754b4b362a`. Graphics code is unchanged;
the prior Vulkan validation remains applicable. Logs:

- `build-logs/stage25-sdl-build.log`
- `build-logs/stage25-sdl-host.log`
- `build-logs/stage25-native-tests.log`
- `build-logs/stage25-ctest.log`

Commands used:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled --parallel 4
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" LD_LIBRARY_PATH="$PWD/local/host-libs" python3 -m unittest tests.integration.test_native_bootstrap.NativeBootstrapTests.test_sdl_controller_events_reach_guest_reports_and_release_on_disconnect -v
```

Changed files: `cmake/native_targets.cmake`, `runtime/src/app/main.cpp`,
`runtime/include/kirby/input/wpad.h`, `tests/integration/controller_probe.cpp`,
`tests/integration/test_native_bootstrap.py`, project/runtime READMEs and this report.
No translation, game data, vendor code or native hook changed. Source attributions
and licenses remain intact. The game window remains closed.

Next: capture physical SDL/PAD/guest button transitions during the user's reported
failure, including focus/capture state, then correct the first reproduced mismatch.
The headless virtual-device checks do not resolve the physical controller report.
Fountain Gardens and physical controller gameplay remain unverified.

## Stage 26: physical controller capture without the game window

The SDL diagnostic now has a `--monitor-controller` mode. It uses the same host
device initialization, SDL dispatch and guest WPAD serializer as the Stage 25 probe,
without attaching a virtual controller. For 30 seconds it logs changed SDL/PAD/guest
button masks, connection result, PAD blocking and router capture state for all four
ports. The isolated command in the project README keeps its input preferences and
NAND initialization outside the user's persistent data. The mode does not enter the
game, create a window or initialize graphics.

The native capture enumerated the physical USB DualSense (VID 054C/PID 0CE6, SDL
type 6) on port 0 with error 0, blocked 0 and captured 0. Ports 1–3 were disconnected.
No button transitions occurred during the capture. A separate 60-second read of the
Linux joystick device `/dev/input/js0` recorded analog-axis jitter but no button or
D-pad hat events. These observations establish neutral physical-device sampling;
they do not establish directional mapping, action buttons or gameplay response.
The requested manual press sequence remains pending. An exit status of zero from
the monitor only means its capture completed.

Static inspection of the pinned Aurora ImGui layer showed that it captures only
keyboard and mouse sources; its controller path returns Pass. This is evidence
about that layer's implementation, not a reproduction of the user's reported
failure. The graphical window's focus/pause behavior has not been exercised here.
No input mapping was changed and no cause for the black window is claimed.

For the reviewed horizontal layout, isolated presses should produce these masks:

| Physical button | SDL button mask | PAD mask | Guest Wii mask |
| --- | --- | --- | --- |
| Left | `0x2000` | `0x0001` | `0x0008` |
| Right | `0x4000` | `0x0002` | `0x0004` |
| Up | `0x0800` | `0x0008` | `0x0002` |
| Down | `0x1000` | `0x0004` | `0x0001` |
| Cross / south | `0x0001` | `0x0100` | `0x0100` |
| Circle / east | `0x0002` | `0x0200` | `0x0200` |

The native build ID is `203f4788dfdcdeb54f077c4f19d061bce495ccab`. The physical
monitor exited normally after its 30-second deadline. All 34 Python/native checks
pass again (`build-logs/stage26-native-tests.log`). No graphics code changed, so GPU
checks were not repeated. The original FLF.bin still has the Stage 24 SHA-256.
Other local logs: `stage26-input-build.log`, `stage26-physical-monitor.log` and
`stage26-dualsense-kernel.log`, all under `build-logs/`.

Commands used: the CMake build and Python discovery commands from Stage 25, followed
by the isolated physical-monitor command now in the project README. The kernel
capture opened only the controller's joystick device, read its axis/button maps and
recorded events for 60 seconds; it did not grab the device or inject input.

Changed files: `runtime/src/app/main.cpp`, `runtime/include/kirby/input/wpad.h`,
`tests/integration/controller_probe.cpp`, project/runtime READMEs and this report.
No vendor code, translation, native hook or gameplay mapping changed. The game window
remains closed. Next: capture actual directional/action presses and compare the
three input boundaries, then reproduce any remaining failure in the game.

## Stage 27: native graphics diagnostics with a hidden window

Added `GxRenderer::OpenWindow(bool startHidden = false)` and `--video-hidden`.
The explicit flag uses the Vulkan renderer, event pump, GX transport and VI path,
forwarding Aurora's existing `startHidden` setting. It requires the host graphics
session and is distinct from `--headless`. No Aurora/vendor patch, gameplay hook
or translation changed.

The GPU probe accepts `--gpu-xfb-hidden`. It checks that the SDL window is hidden,
runs the copy/draw/texture/depth checks, then submits 16 more XFB/VI frames with
30 ms between them so fast test submissions cannot all be dropped by display
admission. It passed 294 checks (`stage27-hidden-gpu.log`). Unknown test options
now fail instead of silently running only the register checks.

The native game ran in `local/stage27-hidden-40kjan9m`, using a NAND/SRAM copy with
xattrs preserved. The original FLF.bin still has the Stage 24 SHA-256. The explicit
hidden run uses build ID `1a1173c310497bce5fb236863e88c310ca4f8d56`. Verified
captures under ignored `build-logs/`:

- Wrist-strap screen: `stage27-first-xfb.png`.
- Sideways-controller notice: `stage27-after-confirm.png`.
- Real main menu: `stage27-current.png`.
- File 1 selector/confirmation: `stage27-file-selector.png`,
  `stage27-file-confirm.png`.
- Quilty Square at the Patch Castle doorway after loading File 1:
  `stage27-world.png`.

An ignored GDB helper reads the VI top XFB address from the native device model and
reads the verified 640x480 YUYV buffer from guest RAM. PNG output decodes those bytes
with studio-range BT.601 coefficients; it adds no game content. Short virtual PAD
pulses selected normal menu options and each override was cleared. Physical
controller gameplay remains unverified.

The initial debugger-configured hidden run was sampled inside a Vulkan acquisition
wait. That alone did not establish a deadlock: the explicit hidden run had completed
609 VI presentations when sampled in the same kind of wait, then reached the menu
and reloaded File 1. The initial run was explicitly terminated after captures;
the explicit run remains live for isolated gameplay checks. No production graphics
fix is claimed, and the user's black-window report remains unexplained.

All 34 Python/native checks and 20 selected CTest cases pass (`stage27-native-tests.log`,
`stage27-ctest.log`). Build logs: `stage27-native-build.log` and
`stage27-hidden-test-build.log`. Runtime log: `stage27-hidden-explicit.log`.
The working directory and PID are recorded in `local/stage27-hidden.json`.

Commands used, in addition to the existing Python/native and CTest commands:

```bash
bash tools/container.sh cmake --build build/native --target kirby_gx_renderer_test --parallel 4
bash tools/container.sh cmake --build build/native --target WiiCompiled --parallel 4
LD_LIBRARY_PATH="$PWD/local/host-libs" timeout -s KILL 15s build/native/kirby_gx_renderer_test --gpu-xfb-hidden
```

The game was launched with absolute executable/DATA paths from the isolated
directory and `--video-hidden`. No visible game window was reopened.

Changed files: `runtime/include/kirby/graphics/gx_renderer.h`,
`runtime/src/graphics/gx_renderer.cpp`, `runtime/src/app/main.cpp`,
`tests/unit/graphics/gx_renderer_test.cpp`, project/runtime READMEs and this report.
Next: continue the normal Patch Castle route toward Fountain Gardens in the isolated
run. Physical button validation remains pending.

## Stage 28: Patch Castle completion and Fountain Gardens gameplay

The Stage 27 hidden native executable, build ID
`1a1173c310497bce5fb236863e88c310ca4f8d56`, completed Patch Castle using normal
virtual PAD presses. Verified actions include grabbing/throwing yarn objects,
submarine swimming, ladder climbing, behind-fabric door traversal, pendulum
swing/release, the weight transformation, car dash and ringing the Bonus Bell.
The clear screen recorded 435 beads, a 65-bead streak and the Chandelier treasure.
No player coordinates, inventory, completion flags or game code were patched.

The game committed its save through GetAttr, temporary-file creation and Rename.
After the normal post-stage sequence and housing introduction, Kirby left the room,
walked into Grass Land, selected Fountain Gardens and entered the level. Read-only
observations of PlayerBase, alongside actual XFB captures, recorded:

| Input | X | Y | State |
| --- | ---: | ---: | ---: |
| Initial position | 15.8500 | 20.9250 | 1 |
| Right | 20.0500 | 20.9250 | 1 |
| Left | 17.4900 | 20.9250 | 1 |
| Jump | 17.4157 | 24.2506 | 2 |

The observation helper reads GameManager at `0x808E4D38`, player 0 at manager
offset `0x74`, position at player offset `0x0C` and state at offset `0xC10`.
These offsets were checked against Swiftshine declarations and the original
translated accessors. They are used only for observation, not for movement.

Evidence remains local under ignored `build-logs/`: `stage28-bell-ring.png`,
`stage28-fountain-select.png`, `stage28-fountain-start.png`,
`stage28-fountain-right.png`, `stage28-fountain-left.png`,
`stage28-fountain-jump.png` and `stage28-fountain-movement.log`.
Captures decode the actual native VI XFB using the Stage 27 method.

The original ordinary-launch save remains unchanged at SHA-256
`fc034a13879ec25699424fb95758b4e32ee8ef4861083bbe5814a7553dc1767e`.
The isolated progression save is 43,456 bytes with SHA-256
`f81d6cdbfb206b7bbe7cbe3a4d2010dfc82ea6c5373fe1fb2c272381a5a28bf7`.
Its source directory is `local/stage27-hidden-40kjan9m/local/user/`.
An independent hidden restart in `local/stage28-reload-usb6x758` used a copy of
that NAND/SRAM with xattrs preserved and no copied Aurora cache. File 1 showed 1%
and 00:36:33, loaded into Quilty Square outside the unlocked Kirby's Pad with 600
beads, and opened Area Selection. This confirms saved progression survived a cold
restart; re-entering Fountain Gardens after that restart was not tested.

The local GDB input/capture helpers used this command pattern from the repository
root, with the target PID and output path chosen for each observation:

```bash
gdb -q -batch -p 268020 \
  -ex 'source local/pulse-timed.py' \
  -ex 'python pulse(0x2,3)' \
  -ex 'source local/capture-xfb.py' \
  -ex 'python capture("build-logs/stage28-example.png")' \
  -ex detach
```

`pulse` calls Aurora's `PADSetVirtualStatus`, lets the native process run for the
requested host interval, then calls `PADClearVirtualStatus`. Masks `0x2`, `0x1`
and `0x100` are host right, left and south/jump. The helpers and captures remain
local, as do all generated/game files. The recorded PIDs are historical.

Physical monitoring completed 27 further 30-second intervals with no button
transitions. Twenty completed intervals are recorded in
`stage28-physical-watch-detached.log`; the earlier watcher ended after seven.
Neutral sampling does not validate the user's DualSense presses, directional
mapping or gameplay. No production input mapping changed in this stage. The
existing SDL virtual-device test exercises directions and face buttons, but the
physical nonresponse/reversed-direction report remains unresolved.

At the user's request, both remaining hidden test processes were stopped. Neither
exited within two seconds of SIGTERM, so SIGKILL was required. This reproduces a
shutdown limitation; no clean-shutdown fix is claimed. No visible window was
reopened, and no game process remains running.

Only the project/runtime READMEs and this report changed in this stage. No runtime,
vendor, translation, hook or license changed. The unchanged binary retains the
Stage 27 validation results: 34 Python/native tests, 20 selected CTest cases and
294 hidden Vulkan checks. These suites were not rerun for documentation changes.
Next: capture intentional physical DualSense presses with `--monitor-controller`,
compare SDL/PAD/guest masks, and reproduce the first mismatch before changing the
mapping. The user's physical controller and Legion Go remain unverified.

## Stage 29: positional Xbox controller defaults

Extended the existing headless SDL integration fixture to attach standard, PS5
(`054C:0CE6`), Xbox 360 (`045E:028E`) and Xbox One (`045E:02D1`) virtual gamepads.
The fixture verifies SDL's detected type before exercising each preset, then checks
four directions, four face buttons, plus/minus, release and unplug while held
through the real SDL router, PAD backend and guest WPAD serializer.

Before the fix, standard and PS5 passed, but Xbox 360 east failed:
SDL reported the button pressed, routing was active and uncaptured, PAD was
`0x0400`, and the guest report was `0x0400` instead of the required Wii 1 mask
`0x0200`. The pinned Aurora GameCube defaults swap east/west on Xbox 360 and Xbox
One, whereas Kirby's horizontal bridge expects positional face buttons.
The failure is retained in `build-logs/stage29-presets-before.log`.
This is a reproduced Xbox preset issue, not a reproduction of physical DualSense
nonresponse or reversed D-pad behavior.

`runtime/src/input/controller_mapping.cpp` now installs positional Xbox 360/One
defaults before `WPADInit` calls `PADInit`. The D-pad, trigger and analog-axis
defaults retain Aurora's existing values. Aurora's normal saved-mapping loader
still takes precedence; no user mapping files were rewritten. Standard and PS5
defaults were not changed. No guest address, native registration, vendor source,
translation or game data changed. The adapted default tables retain Aurora's MIT
notice and Luke Street's attribution in the source and `THIRD_PARTY.md`.

The regression passed after the fix for all four detected profiles, including the
same east press that previously failed. All 34 Python/native tests and 20 selected
CTest cases passed on the new build ID
`96cfd7e23e0f942322c701a8f0e19a17fdd869e6`. Relevant commands:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled --parallel 4
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest tests.integration.test_native_bootstrap.NativeBootstrapTests.test_sdl_controller_events_reach_guest_reports_and_release_on_disconnect -v
bash tools/container.sh env KIRBY_NATIVE_TEST=/workspace/build/native/WiiCompiled \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
```

Logs: `stage29-input-test-build.log`, `stage29-input-build.log`,
`stage29-presets-after.log`, `stage29-native-tests.log` and `stage29-ctest.log`, all
under ignored `build-logs/`. Graphics implementation did not change, so the full
GPU suite was not repeated. A new isolated `--video-hidden` game run did execute
the new controller initialization, load the AXWii task and render the wrist-strap
screen. The actual native XFB is `stage29-hidden-first-xfb.png`, with runtime and
capture logs `stage29-hidden-boot.log` and `stage29-hidden-first-xfb.log`.
Its copied save/configuration lives in `local/stage29-hidden-64i322_b`.
The test was stopped after capture; SIGTERM again timed out and SIGKILL was required
(`stage29-stop.log`). No game process or visible game window remains open.

Changed files: `cmake/native_targets.cmake`, `runtime/include/kirby/input/wpad.h`,
`runtime/native-src/input/wpad.cpp`, the new `runtime/src/input/controller_mapping.cpp`,
`tests/integration/controller_probe.cpp`, `tests/integration/test_native_bootstrap.py`,
project/runtime READMEs, `THIRD_PARTY.md` and this report.
Next: record intentional physical presses using the existing monitor, then test the
same actions in the game. Virtual preset coverage does not establish physical
DualSense input, focus behavior or Legion Go compatibility. The black-window report
and clean-shutdown limitation remain unresolved.

## Stage 30: host exit propagation and audio teardown order

The three runtime calls to `aurora_update()` discarded its returned events.
Pinned Aurora maps both SDL quit and window-close requests to `AURORA_EXIT`;
pinned SDL maps SIGTERM to a quit event. The ignored event therefore explained
the previously reproduced SIGTERM timeouts.

The GPU regression queues a real SDL quit event and presents a newly copied XFB.
Before the fix it failed check 297, "Native video loop propagates an SDL quit
request" (`stage30-quit-before.log`). The shared host event helper now inspects
Aurora's returned list and throws `Kirby::ExitRequested` after the noexcept update
returns. Window initialization, VI presentation and WPAD polling all use it.
The existing continuation exception path carries it back to the root stack; `main`
handles it as normal exit rather than reporting an unsupported Wii operation.
The hidden Vulkan regression then passed 297 checks (`stage30-quit-after.log`).

An additional integration test boots the real native executable in a temporary
directory with `--video-hidden`, waits for controller initialization, AXWii task
loading and the first GX draw, then sends SIGTERM and requires status 0. With only
event propagation fixed, this failed: the exit request reached `main`, but teardown
reported "corrupted size vs. prev_size while consolidating". The failing log is
`stage30-native-quit-before-cleanup.log`.

A GDB reproduction stopped at SIGABRT before the crash handler. The native stack
was `SDL_DestroyAudioStream_REAL` -> `AudioBackend::Shutdown` ->
`Kirby::ShutdownAx` -> `main` (`stage30-exit-backtrace.log`). Source inspection
confirmed the destruction order: the renderer called Aurora shutdown, whose window
shutdown calls `SDL_Quit`; SDL's `SDL_QuitAudio` destroys its remaining streams.
The later StackCleanup then tried to destroy the owned audio stream again.
Declaring StackCleanup after the renderer makes its destructor run first, releasing
the audio backend and native guest continuations before global SDL teardown.
No upstream patch or workaround that skips destruction was added.

After correcting that order, the native SIGTERM test passed and all 35 host
Python/native tests passed, including that Vulkan integration test. The 20 selected
CTest cases also passed. Current build ID:
`457ebe834117dbbdd1ffc3be3ece8f634994521e`. Commands from the repository root:

```bash
bash tools/container.sh cmake --build build/native --target WiiCompiled kirby_gx_renderer_test --parallel 4
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/kirby_gx_renderer_test --gpu-xfb-hidden
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" \
  LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
```

Logs remain under ignored `build-logs/`: `stage30-exit-build.log`,
`stage30-cleanup-build.log`, `stage30-native-quit.log`,
`stage30-all-native-tests.log`, `stage30-ctest.log` and the reproduction logs above.
The video integration test is opt-in through `KIRBY_NATIVE_VIDEO_TEST`; ordinary
discovery skips it without that variable and does not require a host graphics
session. It uses disposable save/controller configuration and kills a stuck child
in its failure cleanup. No game test process remains running.

Changed files: new `runtime/include/kirby/app/events.h`,
`runtime/src/app/main.cpp`, `runtime/src/graphics/gx_renderer.cpp`,
`runtime/src/graphics/gx_xfb.cpp`, `runtime/native-src/input/wpad.cpp`,
`tests/unit/graphics/gx_renderer_test.cpp`, new
`tests/integration/test_native_video.py`, project/runtime READMEs and this report.
No native hook, translated function, vendor source, attribution or game data changed.
The test establishes startup/SIGTERM cleanup, not a long gameplay shutdown soak
or a visible window-manager click. Those follow the same reviewed exit-event path
but were not exercised here. Physical DualSense input and the user's black-window
report remain unresolved; intentional physical presses are still required to
validate the controller path in the game.

## Stage 31: performance findings, MEM2 audio repair and a native launcher

Reviewed ZeldaWWHDRecomp, Mario Kart WiiCompiled, WarioRecomp, New Super Paper Bros.,
Brawl Recompiled and the previously reviewed Resort sources. Pinned links and reuse
decisions are in `docs/references.md`. Zelda's reference is a Wii U/Cafe/GX2 port;
its renderer and guest hooks are not a Wii drop-in. Its SDL3/ImGui frontend pattern
and player-focused documentation are applicable. WiiCompiled is already the pinned
base, and Wario remains the closest Good-Feel comparison. No new game-specific
addresses or source from those ports were imported.

### Measurements and rendering change

A live visible-run sample counted 416 VI submissions in 10.0308 seconds, or 41.47/s,
with 421 texture copies. A different scene after indexed caching counted 38.21/s
without texture copies. These are not comparable before/after scenes, and no FPS
gain is claimed from those samples. During the earlier run the process aborted with
`ByteBuffer overflow: 8391328 bytes requested, mapped capacity 8388608`.

Kirby's adapter invalidated indexed-array uploads every draw and on redundant CP
base bindings. It now checks the referenced RAM bytes against a snapshot, preserving
the current frame's upload when unchanged. Changed RAM, addresses, larger extents
and frame retirement still require upload. The new GPU check failed before each
part of the fix and passed afterwards; the existing changed-RAM pixel test still
passes. This removes redundant uploads without disabling CPU visibility, but the
original scene's overflow has not been replayed to prove all capacity issues solved.

Thirty render-worker stack observations included 21 waiting in NVIDIA/Dawn swapchain
acquisition and eight in its idle queue. A subsequent same-process hidden-window
VSync A/B used one-second settling and eight-second observations: on 19.94/s,
off/Mailbox 39.73/s, on again 19.93/s. This implicates presentation waiting in the
fixture; it is not a visible gameplay or Legion Go benchmark. `--no-vsync` and the
launcher checkbox expose the existing Aurora choice, retaining VSync-on by default.
The guest clock is unchanged. There is no validated 30 FPS mode or sustained 60 FPS
claim. Full methodology and limits are in `docs/performance.md`.

A further experiment removed the wait after VI submission to overlap the producer
with presentation. Interleaved file-selector samples with Mailbox were 34.87/s
(synchronous), 35.36/s (asynchronous), and 34.02/s (synchronous). This was not a
conclusive improvement; the experiment, diagnostic override and its concurrency
fixture were reverted. The submitted implementation retains the wait. Its local
logs remain `stage31-overlap-*`; the transient 299-check GPU result is not the final
suite count. Subsequent TID-based samples point to a mixture of scheduler sleeps,
copy/readback and CPU rendering work after FIFO throttling is removed.

### Audio root cause and repair

PipeWire showed the native stream unmuted at 100%, but DMA buffers in menu/game
samples had absolute peaks 0, 1 or 7. Active parameter blocks contained streaming
voices. A live GDB check showed `AxDspHle::g_mixMem2 == nullptr` while MEM1 was mapped.
`Memory::kMem2Size` is the upstream 128 MiB NDEV limit; Kirby's audited map is 64 MiB.
The mixer's full-range lookup therefore failed, and MEM2 sample fetch returned zeros.

`patches/wiicompiled/ax-retail-memory.patch` obtains the configured cached MEM2 region
from `Memory::DescribeRegions`, retaining its byte extent for range checks and the
DSP wrap mask. It does not expand guest memory or change any Kirby address. The
adapted snapshot keeps upstream GPL notices; the checked-in patch reproduces it.

The AX integration regression failed before the repair on `ReadAramByte` from MEM2.
It now checks the first fixture address, the last retail MEM2 byte and rejection
past its boundary. It also submits a complete synthetic PCM16 voice in MEM2 with
unity gains, using RK5E01 AXPB offsets from the CC0 headers, and verifies all 192
stereo output samples equal 1000. The existing original-task/IRQ/mailbox/bus test
remains intact. The real game subsequently produced 192 nonzero DMA samples with
peak 3529 and nonzero active ADPCM histories. This proves the silent source was
repaired; listening, queue-overflow behavior and audio/video synchronization still
need validation. The hidden audio/performance process was closed normally afterward.

### Frontend and documentation

`launcher/main.cpp` builds as `build/native/kirby-launcher`, using the existing
SDL3/ImGui libraries and their original backends. It selects an extracted DATA
folder, checks required paths and the exact DOL SHA-1, starts the native game with
an argument vector, saves volume/VSync choices and opens save/log/build-guide paths.
Verification uses `sha1sum` through `posix_spawnp`, without shell interpolation.
The game is a separate process, polled without blocking the frontend. The runtime
accepts bounded finite `--volume` values and `--no-vsync`. The launcher remains
open for the user and was observed starting the real visible native game.

The launcher is a development frontend, not a complete WWHD-style installer. It
does not yet extract, translate/build automatically, package releases or provide
validated 30/60 FPS presets. Its runtime-dependent settings are applied next start.
The build helper now includes the launcher target. A source-tree-relative RPATH
finds the existing local Bazzite compatibility library; no host package was installed.

The README now covers starting, controls and known limitations. The former detailed
commands remain in `docs/building.md`; new `docs/performance.md` and `CHANGELOG.md`
separate measurements from user instructions. No AI plan/design artifacts, game
assets, generated game code or captures were added to Git. Source/third-party
attribution remains in `THIRD_PARTY.md`.

### Verification and reproduction

Final native build ID: `48e9e21cfb1d871a51813c39efed5d4d6a4d11ad`.
All 41 Python/native/frontend checks pass, including hidden startup/exit with
VSync on and off, wrong/incomplete dump rejection, frontend rendering and invalid
volume rejection. The 20 selected CTest cases and 298 hidden Vulkan checks pass.
The build helper's configure-only run verified patch applicability/reversal.

```bash
bash tools/container.sh cmake --build build/native \
  --target WiiCompiled kirby-launcher kirby_gx_renderer_test --parallel 4
bash tools/container.sh python3 tools/build_native.py --configure-only
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_LAUNCHER_TEST="$PWD/build/native/kirby-launcher" \
  LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/kirby_gx_renderer_test --gpu-xfb-hidden
build/native/kirby-launcher
```

Logs and captures remain local under `build-logs/stage31-*`, including
`final-tests.log`, `final-ctest.log`, `final-gpu.log`, `present-ab.json`,
`aram-before.log`, `voice-after.log` and the launcher captures. Earlier build/test
logs retain failed assertions rather than replacing them with a success claim.

Changed source/build files: `launcher/main.cpp`, `cmake/native_targets.cmake`,
`tools/build_native.py`, `runtime/src/app/main.cpp`,
`runtime/include/kirby/graphics/gx_renderer.h`, `runtime/src/graphics/gx_renderer.cpp`,
`runtime/src/graphics/gx_draw.cpp`, `patches/wiicompiled/ax-retail-memory.patch`,
`tests/integration/ax_probe.cpp`, `tests/integration/test_native_bootstrap.py`,
`tests/integration/test_native_video.py`, new `tests/integration/test_launcher.py`
and `tests/unit/graphics/gx_renderer_test.cpp`. Documentation changes are listed
above plus `runtime/README.md`, `docs/references.md`, this report and attribution.

Next concrete performance check: run the same visible Fountain Gardens route with
VSync on/off, measure frame-time distributions and logic speed, then time copy
readback and presentation separately. Verify real speaker output after the MEM2
repair before describing audio as complete. Physical controller/Legion Go validation
and the known preempted-context startup limitation remain open.
