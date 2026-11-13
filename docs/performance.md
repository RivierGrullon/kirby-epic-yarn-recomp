# Performance and audio

The targets are 60 FPS (16.67 ms per displayed frame) and a smooth 30 FPS option
(33.33 ms). Neither target is certified. A presentation limit alone does not prove
correct game speed or audio synchronization.

## Measurements

Hardware: Acer Nitro AN515-55, Intel i5-10300H, NVIDIA RTX 3050 Laptop GPU, Bazzite,
native Vulkan. This is not the target Legion Go. The build uses Release with `-O3`.

A read-only GDB observation counted 416 additional native VI presentations in
10.0308 seconds: **41.47/s**, alongside 421 texture copies. A later scene after
indexed upload caching counted 383 in 10.0230 seconds: **38.21/s**, with no texture
copies. Draw counts also differed substantially. These are not comparable A/B
measurements and establish no FPS gain or regression.

These counters count VI submissions, not compositor-displayed frames. They do not
measure percentile frame times, latency or logic steps. GDB briefly stops the game
at interval boundaries. Logs are local: `stage31-live-rate.log` and
`stage31-live-rate-after.log` under `build-logs/`.

### Presentation A/B

A subsequent same-process hidden-window test changed only Aurora's VSync setting,
with one second of settling and eight seconds of observation per interval:

| Sequence | VI presentations/s |
| --- | ---: |
| VSync on | 19.94 |
| VSync off (Mailbox selected) | 39.73 |
| VSync on again | 19.93 |

The matching return to the first result supports presentation waiting as a
bottleneck in this fixture. Thirty stack samples of the render worker included
21 in NVIDIA/Dawn swapchain texture acquisition and eight in its idle queue.
Other worker threads inherited the main-thread name; they must not be counted as
the guest execution thread just by name. Stack samples are diagnostic, not precise
time percentages. The test was hidden, uses this NVIDIA driver and is not proof of
visible gameplay performance or behavior on AMD/Legion Go.

Logs: `stage31-present-ab.log`, `stage31-present-ab.json` and
`stage31-stack-samples.json`. VSync is now selectable in the launcher; direct use:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/WiiCompiled --video --no-vsync --game-data disc/DATA
```

The default remains on. Off selects Aurora's available non-FIFO mode; it does not
change the guest timebase, certify 60 FPS or implement the requested 30 FPS mode.

Removing the producer's wait after VI submission was also tested, using the same
process and file selector with Mailbox presentation. The interleaved synchronous /
asynchronous / synchronous observations were 34.87 / 35.36 / 34.02 submissions/s.
That difference is inconclusive, so the change and its experimental control were
reverted. The submitted code retains the original wait. The local comparison is
`stage31-overlap-ab.json`; intermediate binaries/counters are not release claims.

A separate 30-sample diagnostic of the actual guest thread in that experimental
Mailbox run found seven scheduler sleeps, five readback waits and several XFB
conversions, texture hashes and command-processing sites. The render worker was
idle in 26 samples. This shifts the next investigation toward CPU/copy/timing
costs after removing FIFO throttling; it does not establish one dominant function
or a precise CPU/GPU percentage. Samples identify threads by TID, not inherited names.

## Rendering findings

The game stopped with `ByteBuffer overflow: 8391328 bytes requested, mapped capacity
8388608`. Aurora's storage upload buffer has an 8 MiB capacity. Kirby's adapter
invalidated indexed uploads on every draw and CP base binding, even for unchanged data.

The adapter now compares the referenced RAM prefix against its snapshot and reuses
the current frame's immutable upload when equal. Changed data, a changed base,
larger referenced extent or frame reset requires another upload. The GPU regression
checks reuse and retains the pixel test for changed RAM without rebinding. This
reduces redundant work; it does not prove every scene fits the upload capacity.

Display/texture copies still synchronously read GPU results into RAM. Display copies
now pack YUYV on the GPU; texture hashing, draw submission, readback waits and
presentation waits remain costs to investigate.

### GPU XFB packing

Phase measurements found approximately 1.6–1.9 ms per 640x480 display copy in the
CPU RGBA8-to-YUYV conversion. `patches/aurora/xfb-yuyv-readback.patch` adds a
readback-only format, separate from guest GX format numbers. Its compute shader
packs two adjacent pixels into Y0/U/Y1/V using the existing encoder's integer
coefficients and both rounding steps. Display readback falls from 1,228,800 to
614,400 bytes; guest RAM still receives every copied pixel before GX resumes.
The EFB preservation and render-worker synchronization boundaries remain intact.

The synthetic Vulkan regression compares GPU output with the retained CPU encoder
for varied adjacent colors, widths 16/32 and heights 1/3/4/7. Existing primary-color,
RAM-boundary, texture-copy and EFB-preservation checks remain enabled.

A same-process, hidden Mailbox file-selector comparison used temporary profiling
and a CPU/GPU packing switch, with one-second settling and eight-second intervals:

| Packing | VI submissions/s | Readback ms/copy | CPU conversion ms/copy |
| --- | ---: | ---: | ---: |
| CPU | 37.89 | 5.50 | 1.57 |
| GPU | 39.25 | 5.10 | below 0.001 |
| CPU again | 36.76 | 5.39 | 1.59 |

The fourth interval stopped on a native memory fault, so this is an incomplete
comparison, not a sustained FPS result. Earlier title comparisons also stopped
when VI selected RAM differing from the retained XFB snapshot. Those failures
limit the measurements; neither is claimed fixed by packing. Temporary profiling
and its switch were removed from the final binary. Logs: `stage32-file-benchmark.log`,
`stage32-packing-ab.json`, `stage32-diagnostic-game.log` and
`stage32-file-benchmark-game.log`. All are local.

### Visible FPS counter

The game window title now updates four times per second using `aurora_get_fps()`.
Aurora calculates a rolling rate from successful surface presentations under its
presentation mutex. This is different from the VI-submission counters above.
It is not a compositor scanout measurement or a simulation/audio speed estimate;
hidden windows may report zero. The launcher starts the latest sibling executable,
so an already running game needs another launch to acquire this counter.

## Audio findings

PipeWire shows an unmuted game stream at 100%, targeting built-in analog output.
The native DMA backend is initialized at 32 kHz stereo. Read-only PCM observations:

| Scene | Samples | Nonzero | Absolute peak |
| --- | ---: | ---: | ---: |
| Wrist-strap screen | 192 | 0 | 0 |
| File selector | 192 | 192 | 1 |
| After starting File 1 | 192 | 192 | 7 |

These observations preceded the repair below. The wrist-strap screen alone is not
an audio test. An open host stream does not establish audible music. Output-queue
overflow has also been observed and still needs pacing work.

### MEM2 sample fetch repair

The mixer used `Memory::kMem2Size` (128 MiB, the NDEV limit) when obtaining its
MEM2 pointer. Kirby maps 64 MiB, so that range failed and `g_mixMem2` remained null.
The voice accelerator consequently fetched zeros from MEM2 even with active voices.
The live pointer capture is `stage31-mem2-audio.log`.

`patches/wiicompiled/ax-retail-memory.patch` reads the configured region extent,
retains it for mixer bounds checks and uses it for the DSP MEM2 wrap mask. It does
not expand guest RAM. The regression failed before the patch on a MEM2 byte fetch;
afterwards it verifies both boundaries and a complete constant PCM16 voice whose
192 stereo output samples are all 1000. Parameter-block offsets are independently
specified by Swiftshine/key's RK5E01 AX headers.

The real game then produced 192 nonzero DMA samples with absolute peak **3529**,
alongside active ADPCM voices and nonzero decoder history. See
`stage31-audio-fixed-title.log` and its native XFB capture. This establishes repaired
source PCM, not a listening test or sustained audio/video synchronization. The
`stage31-aram-before.log` and `stage31-voice-after.log` retain the regression results.

The launcher volume control sets backend gain on the next start. It changes neither
system volume nor the underlying mixer result.

### Reference playback and producer pacing

Dolphin 2609a ran the user's local RK5E01 WBFS with Vulkan, 1x internal resolution,
speed configured to 1.0, cheats disabled and no audio stretching. The title-screen
capture shows **59.92 FPS**. The user reported substantially smoother playback and
normal music. This is a title-screen reference, not a benchmark of every level.
The comparison used an isolated Dolphin user directory and a copy of the local
progression save; normal Dolphin and native settings/saves were left intact.

Temporary native AX counters then measured three six-second intervals in the
hidden Mailbox file selector:

| Interval | AX lists/s | OUTPUT commands/s | DMA blocks/s | VI submissions/s |
| --- | ---: | ---: | ---: | ---: |
| 1 | 69.55 | 69.55 | 4015.09 | 27.62 |
| 2 | 71.67 | 71.67 | 4010.82 | 28.73 |
| 3 | 72.41 | 72.41 | 3982.86 | 28.54 |

`OutputSamples` writes 96 stereo samples per OUTPUT. At 32 kHz, sustaining fresh
PCM needs approximately 333.33 such outputs/s. The second interval had no skipped
DMA blocks, yet produced only about 6,880 fresh stereo samples/s while DMA fetched
about 32,087 samples/s. The producer therefore falls well behind the consumer in
this observation; an initialized/unmuted stream and a full host queue do not prove
correct musical tempo. The first interval includes catch-up from debugger stops
and is less representative. Native observations used brief debugger stops at
interval boundaries; they are not an end-to-end listening measurement.

The counters incremented only for hardware AX command-list dispatches and their
scanned OUTPUT commands. They did not inject mail, callbacks, samples or game
inputs during the measurement. Profiling source was removed after measurement,
and rebuilding restored native build ID `fdb6687351ebea53eade38cd9e17b0e89ff633fb`.
The logs are `stage33-audio-rate.log`, `stage33-audio-rate.json` and
`stage33-dolphin-visible.png`, all local.

The next audio investigation is interrupt/callback delivery and producer
scheduling, including time spent in synchronous render work. The counts establish
insufficient fresh output; they do not isolate which delay causes it or justify
changing the output sample rate to disguise the deficit.

### Interrupt-enable delivery repair

The native `OSRestoreInterrupts` boundary previously polled IOS/WPAD but did not
dispatch pending device IRQs. A regression with a real AX resume reply remained
pending until scheduler idle. The boundary now dispatches enabled IRQs and drains
immediately pending replies before returning to guest execution. A two-reply
regression failed with single-IRQ delivery and passes with draining; masked replies
remain pending and the original SDK context is restored.

Enabling delivery exposed the previous preemption guard during real startup.
Native continuations now save/restore the active IRQ frame together with their
CPU scope and FP environment. The scheduler follows the SDK's preempted branch:
exception entry has already saved the interrupted registers, so it skips
`OSSaveContext` for that matching IRQ continuation. Unmatched preempted contexts
still fail explicitly. Both native startup modes and the 41-test suite pass.

A temporary same-process switch compared boundary delivery off / on / off in the
hidden Mailbox file selector, using the same DSP OUTPUT-mail counter throughout:

| Boundary IRQ delivery | OUTPUT mails/s | DMA blocks/s | VI submissions/s |
| --- | ---: | ---: | ---: |
| Off | 80.11 | 4035.26 | 37.15 |
| On, draining replies | 135.67 | 4005.48 | 44.28 |
| Off again | 73.28 | 3995.59 | 37.97 |

Each interval lasted approximately six seconds. The first includes debugger
catch-up; the other two had no skipped DMA blocks. This supports a material
producer-pacing improvement, while **135.67 remains below the required 333.33
outputs/s**. The first single-IRQ implementation reached 114.46 mails/s in a
separate comparison; it is superseded by the draining implementation. These are
hidden-window submission/mail rates, not proof of stable visible FPS or normal
musical tempo. New listening and gameplay checks remain necessary.

Logs: `stage33-irq-before.log`, `stage33-irq-tests.log`, `stage33-drain-before.log`,
`stage33-drain-after.log`, `stage33-pacing-ab.json` and
`stage33-pacing-drain-ab.json`. The profiling switch and counters were removed
from the final source and executable.

### Overdue audio buffer ordering

The next fault was in native DMA catch-up: one poll consumed all overdue blocks
from RAM, then raised one coalesced AID interrupt. The original guest refill and
DSP callbacks consequently ran too few times while the sink still consumed about
4,000 eight-sample stereo blocks/s. Raising the playback sample rate would not
repair those missing refills.

When native IRQ-state tracking is installed and AID is enabled, polling now stops
at each buffer interrupt. The original handler must return and pending DSP replies
must be serviced before the next overdue buffer is fetched. Standalone device
probes and masked AID retain continuous raw DMA. The sample rate, clock and maximum
catch-up backlog are unchanged. WiiCompiled's existing `Audio_HLE_Tick` similarly
interleaves due buffers and callbacks; Kirby retains its own original SDK handlers
and verified addresses rather than using another title's audio globals.

Tests cover four overdue buffers with distinct refill payloads, active-IRQ
deferral, pending DSP reply ordering, acknowledgment before refill return, and
masked continuous DMA. The original implementation fails the new regressions;
the repaired models pass 296 audio-DMA and 103 DSP checks.

A same-process hidden Mailbox observation in Quilty Square compared an experimental
deferred graphics-copy checkpoint with the existing synchronous copy path:

| Copy path, with ordered audio in both | OUTPUT mails/s | DMA blocks/s | VI submissions/s |
| --- | ---: | ---: | ---: |
| Deferred checkpoint | 332.92 | 3995.39 | 41.95 |
| Synchronous copy, following interval | 333.61 | 4003.94 | 43.94 |

Both six-second intervals had zero skipped DMA blocks. The preceding synchronous
interval included debugger catch-up and is excluded from this table. The measured
producer rate now approximates the required **333.33 outputs/s**. This is not an
audible-tempo, visible-FPS or sustained-playback certification. Earlier Stage 33
file-selector rates were from a different scene and are not a controlled speedup
comparison.

The deferred-copy experiment was removed: it added complexity without a measured
video gain once audio ordering was repaired. Display/texture RAM visibility and
EFB preservation keep the previous synchronous implementation. Final source has
neither the experimental graphics switch nor profiling counters. Evidence:
`stage34-ordered-square-ab.log`, `stage34-ordered-square-ab.json`,
`stage34-ordered-square.png`, `stage34-dma-before.log` and
`stage34-events-before.log`, all local under `build-logs/`.

### Copy-wait experiments

Temporary Release-build timers separated display readback, texture readback,
render-worker waiting, sleep/poll waiting and presentation in a hidden Mailbox
run. One Quilty Square interval measured 4.48 ms/display readback,
5.53 ms/texture readback and 2.45 ms/presentation, at 44.85 VI submissions/s.
These timings include CPU submission, worker synchronization and GPU completion;
they are not isolated GPU execution times. The two readbacks each polled roughly
three times, sleeping about 2.3 ms in total per copy.

Reducing the polling sleep from 1,000 to 100 microseconds in the title sequence
increased polls from about four to nineteen per copy without an established
presentation gain (58.92 / 58.81 / 57.52 submissions/s in the off/on/off sequence).
That change was rejected.

An optional readback-publication callback was then tested with a condition
variable. Its regressions checked publication before notification, lock re-entry,
cross-thread delivery, duplicate sequences and retired streams. The experimental
code passed 20 CTest targets and 339 synthetic GPU checks, but did not establish
a useful performance improvement:

| Same-process sequence | File confirmation submissions/s | Quilty Square submissions/s |
| --- | ---: | ---: |
| Original wait | 45.43 | 44.12 |
| Publication notification | 42.81 | 44.78 |
| Original wait again | 44.62 | 44.30 |

Each interval used one second of settling and six seconds of observation. At
Quilty Square, display/texture readbacks remained approximately 4.4/5.5 ms and
presentation 2.4 ms with either wait. The callback, its patch/tests and profiling
instrumentation were removed. This experiment restored the verified Stage 34
executable, build ID `b44c146a86893768385791fcef7986654f321139`.

Logs/captures remain local: `stage35-title-copy-rate.{json,log}`,
`stage35-notify-file-rate.{json,log}`, `stage35-notify-world-rate.{json,log}` and
`stage35-notify-world.png`. Red/green publication checks are in
`stage35-notify-{before,thread}.log`; restored checks are in
`stage35-restored-{tests,ctest,gpu}.log`. Source experiments remain ignored under
`local/`. The result shifts investigation toward reducing complete GPU/RAM
round trips, with explicit checks for CPU-visible copy data, EFB preservation,
clear ordering and later CPU texture writes, rather than shortening poll sleeps.

## Packed GPU texture-copy reuse (2026-11-13)

Packed, unscaled RGB8-to-RGBA8 texture copies now retain Aurora's existing GPU
allocation after the synchronous RAM readback. Sampling unchanged copies avoids
uploading those bytes back to the GPU. A RAM snapshot comparison before drawing
invalidates the direct GPU view after CPU writes or changes to format, dimensions
or mip count. Other EFB precision modes and padded layouts retain the RAM path.
The retained-destination map is bounded to 16 entries.

Published launcher executable build ID: `6f2094235014af61622e37723f6d92c700fe9673`.
Validation passed 41 Python integration tests, 20 CTest targets and 354 synthetic
Vulkan checks. The GPU checks cover allocation reuse, unchanged-copy sampling
without an upload, displayed pixels after CPU edits, I4 reinterpretation of copied
RGBA8 bytes, and the RGB565 fallback. Logs: `stage36-final-{build,tests,ctest,gpu}.log`
under ignored `build-logs/`.

The native executable was also observed in Quilty Square and Grass Land using
an isolated save and virtual PAD input. Fountain Gardens was not rechecked in
this build. Captures: `stage36-final-screen.png` and
`stage36-final-grassland.png`. The launcher executable was verified byte-for-byte
against the tested candidate (SHA-256
`8e2d6de3959b63768a9cba1295c0f253105e05385b4564a00e5e9d2e424b8c10`).

A subsequent same-process Quilty Square off/on/off comparison measured
43.94 / 44.46 / 44.90 VI submissions/s, with one second of settling and six
seconds per interval. All 268 texture copies in the enabled interval qualified
for retention. Display readback remained 4.1–4.5 ms per copy and texture readback
about 5.6 ms. This establishes that retention is exercised, but does not show a
throughput improvement outside the variation between baseline intervals.
The run used an isolated save, a hidden window, muted audio and no VSync.
It does not establish visible frame pacing or audible quality. Evidence:
`stage36-reuse-rate.{json,log}` and `stage36-reuse2-scene.png`.

This removes redundant allocations/uploads for eligible copies, but retains the
synchronous GPU-to-RAM waits. Reducing those waits with verified copy visibility
is the next performance target. The published executable contains no profiling
switches or counters from this comparison.

## Experimental batched texture readback (2026-11-13)

The renderer now exposes an opt-in deferred texture-copy API. Eligible packed
RGB8-to-RGBA8 copies can remain on the GPU until a display copy or explicit
completion boundary, sharing one readback submission. GPU finish/token commands,
destination reuse and sampling an overlapping RAM subview complete pending
copies. Disabling the API also completes pending work. The queue is bounded to
16 copies and readback payloads to 32 MiB.

This mode is **disabled by default** and is not enabled by the game or launcher.
Callers must complete pending copies before CPU reads or writes of their
destinations. An unexpected CPU write is detected at completion and fails with
an error; this is not a substitute for auditing guest cache and synchronization
boundaries. CPU reads cannot be detected by the RAM snapshot comparison.

The synthetic Vulkan suite passes 379 checks, including one submission for a
texture/display batch, copies retaining different EFB colors, completion on GPU
finish, destination reuse, disabling deferred mode, and sampling an I4 subview
inside a pending RGBA8 destination. The subview regression failed before its
completion check was added. Evidence is in ignored
`build-logs/stage37-alias-{red,green}-{build,gpu}.log`.

An isolated native timing attempt ended before completing the enabled interval.
There is no complete native off/on comparison and no demonstrated gameplay
speedup for this change. The published executable remains the Stage 36 build
identified above. Before enabling batching in the game, audit CPU-visible
completion boundaries, repeat native scene comparisons, and verify gameplay,
frame pacing and audible output.

## Reproduce checks

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/kirby_gx_renderer_test --gpu-xfb-hidden
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_LAUNCHER_TEST="$PWD/build/native/kirby-launcher" \
  LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest discover -s tests -v
```

Compare identical scenes, camera, inputs, settings and warm caches, with several
runs and frame-time distributions. Report visible/hidden runs separately and check
game speed. Synthetic GPU checks establish correctness, not gameplay performance.
