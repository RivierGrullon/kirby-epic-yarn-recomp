This is the Kirby-specific native registration root used by the translator. Its overrides
replace `__ios_Ipc2` at verified RK5E01 address `0x806A8FA0`, preserving the translated public IOS
wrappers and SDK heap, and `OSRestoreInterrupts` at `0x806681C0`, preserving its MSR, CR and GPR
effects while delivering completed IOS callbacks when guest interrupts are enabled.
Completed asynchronous requests also deliver at the transport's return boundary when
interrupts are enabled, matching the original transport's final interrupt restoration.
Masked requests remain queued. This lets SDK busy waits observe completion without
patching their loops; delivery outside explicit boundaries remains separate work.
`services/ios.cpp` registers these addresses directly, without MKW region mapping.
The separate C++ bootstrap in `runtime/src/app/main.cpp` executes the translated game entry while
rejecting the upstream runtime's native and handwritten translated registrations.
SDK HLE addresses, ABIs and implementations must be reviewed for RK5E01 before enabling them
here and retranslating. Full translation is required when overriding a function that was previously
inlined; regenerating shards alone cannot remove those copies. `tools/audit_hooks.py` reports name-based candidates without activating
them. Canonical shared sources remain in `vendor/wiicompiled/runtime`; adaptations affect only
the ignored local snapshot and preserve its license.

`threading/scheduler.cpp` adds audited `SelectThread` (`0x8066B220`) and `OSLoadContext`
(`0x80664450`) boundaries. The original SDK owns the wait/run queues and interrupt
handler. Idle polling invokes the original interrupt dispatcher with a saved guest
context; exception return unwinds only that dispatcher. The selected waiting thread
resumes its existing host call stack when it is the same thread. Selecting another
thread or restoring a context without a registered host continuation stops explicitly.
This is not yet a general preemptive scheduler.

`tools/audits/scheduler.py` pins the reviewed functions and objects against the DOL and
community map. Keep the `CpuContext` parameter name in every native registration
signature identical to its definition: WiiCompiled's effect analyzer uses the signature
name when scanning the body. A mismatch can omit argument-register synchronization.
The native retrace probe exercises this contract through the real dispatcher and
`OSLoadContext` call, and checks CPU state and SDK queue consistency.

`input/wpad.cpp` replaces audited WPAD functions with Aurora's native
controller backend. Original KPAD processing remains translated. The bridge serializes
42-byte core reports to big-endian guest RAM, owns the guest sampling-ring pointers,
and invokes translated callbacks on isolated CPU contexts at interrupt-restoration or
scheduler-idle boundaries. Aurora callbacks only enqueue events, so guest exceptions
do not cross Aurora's `noexcept` event loop. Samples retain their reports while queued.
`tools/audits/wpad.py` checks each function's address, size and DOL digest, plus the
original callback used by the probe. The main registration gate and startup verification
require the audited address/name pair for every hook. Unsupported extension formats
stop explicitly. Other WPAD operations still need review. The horizontal mapping is applied during
serialization; physical validation remains pending.
The native service selects a core Remote; MotionPlus status zero describes that choice.
The original Wii USB/Bluetooth stack is not started by the native WPAD initializer.

`WPADGetInfoAsync` serializes the SDK's 24-byte big-endian information structure.
Failure callbacks run synchronously; successful host completions are queued until an
interrupt-enabled guest boundary. Concurrent requests return the SDK busy result.

`devices/dsp.cpp` wraps `0x80684770` after auditing its DOL bytes. It runs the original
translated task loader, then delivers pending DSP mail interrupts at a complete CPU
boundary. The original AX initialization callback sets the game's ready flag.
The hardware-only mixer adapter does not initialize another game's globals.

The scheduler also delivers due SDK alarm-queue entries through the original
`DecrementerExceptionCallback`, preserving its queue and resume behavior. This runs at
scheduler idle; it is not general DEC emulation or arbitrary-PC preemption.

`graphics/cache.cpp` registers the five audited data-cache range operations:
`DCInvalidateRange` at `0x80663A80`, `DCFlushRange` at `0x80663AB0`,
`DCStoreRange` at `0x80663AE0`, and the no-sync flush/store variants at
`0x80663B10`/`0x80663B40`. `tools/audits/cache.py` verifies the DOL bodies and
community symbols. Native code preserves their register effects and system-call
boundary while completing overlapping deferred GPU copies. The `kirby_cache`
test compares register effects against the locally generated original functions;
`--verify-bootstrap` also exercises the native registry and range observer.
Full translation is required after adding these overrides so previously inlined
cache loops cannot bypass the observer. Deferred GPU copies remain disabled by
default; these hooks alone do not establish whole-game copy coherence.
