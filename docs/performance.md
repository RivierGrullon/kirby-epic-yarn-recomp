# Performance and audio

The targets are 60 FPS (16.67 ms per displayed frame) and a smooth 30 FPS option
(33.33 ms). Neither target is certified. A presentation limit alone does not prove
correct game speed or audio synchronization.

Frame-rate selection and experimental performance optimizations are separate
settings. The 30/60 FPS selector remains deferred until both modes preserve the
original logic cadence and audio tempo. A performance drop must be diagnosed at
its source; lowering a frame cap is not a fix for game slowdown.

## Per-frame diagnostics

Use the native executable's optional CSV recorder to separate presentation work
from the time between submissions:

```bash
mkdir -p build-logs
LD_LIBRARY_PATH="$PWD/local/host-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  build/native/WiiCompiled --video --no-vsync --gpu-resident-copies \
  --game-data disc/DATA --frame-log build-logs/frames.csv
```

Choose a new filename for each run. The recorder refuses to overwrite an existing
file or follow a destination symlink. It requires a video mode and does not change
pacing, VSync, game clocks or audio. It is a command-line diagnostic, not a launcher
setting. Exit normally to flush the final rows; a crash can leave a partial row.

Each row describes one newly selected XFB generation submitted by `PollVideo`.
Timestamps are host-monotonic nanoseconds from SDL initialization:

| Columns / difference | Meaning |
| --- | --- |
| `sequence`, `vi_field` | Native submission number and emulated VI field count |
| `seal_start_ns - start_ns` | XFB selection, pending-copy handling and RAM validation |
| `seal_end_ns - seal_start_ns` | Aurora frame sealing and queue submission |
| `wait_end_ns - seal_end_ns` | Waiting for the render worker after submission |
| `finish_ns - wait_end_ns` | Host events, window-title updates and next-frame preparation |
| Current `start_ns` minus previous `finish_ns` | Everything between submissions, including guest execution, device polling, scheduler waits and CSV overhead |
| Consecutive `start_ns` values | Intervals between native VI submissions |
| `surface_presents` | Cumulative successful Aurora surface presents; not compositor scanout |
| `readbacks`, `validation_bytes`, `draws` | Cumulative renderer counters; subtract adjacent rows for per-submission demand |
| `queued_pipelines` | Pending pipeline count at the end of the observed submission |

The worker wait includes host/driver activity; it is not a GPU timestamp query.
Validation bytes describe requested comparisons, not measured memory bandwidth.
A zero pipeline queue at sampling time does not exclude work completed earlier in
the interval. Neither submission nor surface-present counts establish game logic
speed, audible synchronization or physical display cadence.

Logging is disabled by default. The CSV buffers writes, flushing the first row
and every 120 submissions. Compare warmed, matching scenes without a debugger or
concurrent compilation, and account for logging overhead. Discard intervals with
debugger stops or scene transitions rather than treating them as gameplay stalls.

Analyze a completed log with the source-only tool:

```bash
python3 tools/frame_stats.py build-logs/frames.csv --start 5 --end 15
```

Window bounds are seconds on the CSV's SDL clock, inclusive at the start and
exclusive at the end. Choose them for the scene being tested; 5–15 is only an
example. JSON output includes linearly interpolated p50/p95/p99, submission rate,
stage distributions and the five longest intervals. The long-interval breakdown
uses the previous row's presentation stages plus the gap up to the current row's
start. Counter deltas are measured at row completion, a different boundary.
`--long-interval-ms` changes the report threshold (25 ms by default), not the game.
Missing rows, partial records, reversed timestamps and decreasing counters fail
explicitly. The tool does not silently remove stalls or repair damaged logs.

The initial recorder check captured 599 submissions during host seconds 5–15 of
a hidden startup run: median interval 16.718 ms, p95 17.437 ms and p99 17.701 ms.
Mean worker wait was 4.083 ms; there were no additional readbacks. This validates
the recording path only: it was not a verified Fountain Gardens scene, a visible
performance test or an audio test. A later debugger-assisted input/capture attempt
was interrupted and faulted during an inferior call; that portion was rejected.
Local evidence: `stage47-metrics-frames.csv`, `stage47-startup-metrics.json` and
`stage47-metrics-start.log` under `build-logs/`.

The next run reached Fountain Gardens through virtual PAD input and verified
right/left movement and a jump. At section 1, position `(70.281, 24.178)`, two
stationary 20-second windows with experimental copies and VSync off measured:

| Window | VI submissions/s | Median interval | p95 | p99 | Maximum | Intervals over 25 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hidden | 59.887 | 16.712 ms | 17.502 ms | 17.718 ms | 32.119 ms | 1 |
| Visible and focused | 58.638 | 16.696 ms | 17.717 ms | 33.161 ms | 50.529 ms | 24 |

Position stayed constant and neither interval added readbacks. Pipeline queues
were empty at sample times; this alone does not rule out synchronous compilation.
In the longest visible interval, 47.608 of 50.529 ms elapsed between submissions;
the previous worker wait was only 0.150 ms. The five longest visible intervals
all had at least 28.755 ms between submissions. That interval includes guest work,
device polling, scheduler waits and host descheduling, so it does not yet identify
which component stalled. The near-60 average is not evidence of smooth delivery.
These are stationary native submission measurements with muted audio, not a
moving-gameplay or synchronization certification. No debugger was attached during
the measured windows. Source: `stage48-{hidden,visible}-analysis.json`,
`stage48-metrics-frames.csv`, `stage48-player.json` and `stage48-tower.png` in
ignored `build-logs/`.

### Rejected asynchronous presentation experiment

After batch texture validation, 19 of 80 intrusive main-thread stack samples
landed in the render-worker wait from `PollVideo`. Removing that one wait was
tested with all readback fences retained. In a fixed visible Fountain Gardens
section-1 position, synchronous / asynchronous / synchronous runs measured
59.133 / 57.132 / 59.130 submissions/s. Their p99 intervals were
33.420 / 35.701 / 32.232 ms. Each phase lasted 15 seconds; position, pipeline count
and readback count stayed constant. This regressed the visible fixture, so the
candidate was rejected and the synchronous wait remains in the runtime.

The asynchronous candidate passed 430 synthetic Vulkan checks; that did not
establish a performance benefit. The hidden comparison varied between baselines
and was inconclusive. Local evidence: `stage47-stack-samples.json`,
`stage47-visible-probe-periods.json`, `stage47-async-probe-periods.json` and
`stage47-async-gpu.log` under `build-logs/`. No asynchronous control was added to
the launcher.

## Shader texture-usage analysis

Further visible, stationary Fountain Gardens diagnostics used read-only
`/proc/PID/task/PID/schedstat` and the native presentation counter. During a
30-second observation, the longest sampled interval consumed 33.797 ms of main
thread CPU time in 39.779 ms of wall time; run-queue delay was 0.206 ms. The
pipeline counters stayed at zero queued / 302 created. These 2 ms polls have
sampling error and overhead; they identify CPU work as a contributor rather than
certifying display timing. Local evidence: `stage48-cpu-observe.json`.

A separate 20-second `perf` capture at 199 Hz collected 2,755 user-space samples
from the main thread, with no lost samples. Memory moves accounted for 12.34% of
samples, comparisons 5.30%, and `populate_pipeline_config` 4.72%. Of the latter's
130 samples, 121 came from Kirby's per-draw preparation and nine from Aurora's
cached draw preparation. These percentages describe sampled CPU work, not wall
time or the frequency of a specific stall.

The renderer now reuses the sampled-texture mask within a synchronous GX batch.
It derives the mask again when Aurora marks the pipeline dirty, the vertex format
or primitive changes, the batch ends, or a renderer write advances the validation
epoch. The batch boundary matters: native GX work outside the decoder can consume
Aurora's dirty flags, so those flags alone cannot validate this cache. Texture
contents, palette checks, rendering and presentation synchronization are unchanged.
This reuses CPU analysis; it does not precompile shaders or skip game frames.

A same-process visible comparison at Fountain Gardens section 1, position
`(70.119, 24.130)`, used 20-second analysis windows and an independent approximately
29-second CPU/counter window for each phase:

| Analysis reuse | CPU ms / submission | Analyses / submission | VI submissions/s | p95 interval | p99 interval | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Off | 11.168 | 1,893.3 | 58.938 | 17.766 ms | 33.209 ms | 34.297 ms |
| On | 10.414 | 383.5 | 59.090 | 17.651 ms | 32.544 ms | 67.037 ms |
| Off again | 11.004 | 1,892.9 | 59.040 | 17.767 ms | 33.074 ms | 34.358 ms |

The CPU cost returned toward baseline when reuse was disabled. Average submission
rate changed little, and the enabled phase included a 67 ms outlier, so this first
comparison does not establish smooth delivery. Position remained fixed, there
were no readbacks, and pipeline counts stayed at zero queued / 302 created.

Because of the outlier, a further on/off comparison used 40-second frame windows
and approximately 49-second CPU/counter windows in the same process and position:

| Analysis reuse | CPU ms / submission | VI submissions/s | p99 interval | Maximum | Intervals over 25 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| On | 9.872 | 59.714 | 17.998 ms | 49.454 ms | 8 |
| Off | 11.165 | 58.964 | 33.084 ms | 82.683 ms | 33 |

Pipeline counts again stayed at zero queued / 302 created, with no additional
readbacks or position changes. Both comparisons reduced CPU time with reuse;
the longer one also improved interval delivery. The change is retained, but
isolated long intervals remain in both modes. This is not stable-60 certification,
a moving-gameplay test, an audio test or Legion Go performance evidence. Full
records, including the first enabled run's outlier, are preserved in
`stage49-shader-comparison.json` and `stage49-shader-repeat.json`.

The retained implementation passes 435 Vulkan checks, including exact pixels after texture usage
changes inside a batch, 51 Python tests including native integration, and 21 CTest
targets. A new repeated-draw check first failed against the per-draw implementation.
The comparison switch and analysis counter are diagnostic C++ interfaces; there
is no additional launcher setting.

The executable installed for the launcher was compared byte-for-byte with the
tested candidate. Its SHA-256 is
`86618897ceecc5c8139de1e8c68018423ec2cc942f427211a47f111c8835cf67`.

Local evidence includes `stage48-main.perf.data`, `stage48-main-perf-flat.txt`,
`stage48-main-perf-stacks.txt`, `stage49-shader-comparison.json`,
`stage49-shader-frames.csv` and `stage49-tower.png` under ignored `build-logs/`.
The Fedora `perf` package was extracted under `local/tools/perf`, with its package
signature verified; no system package or permission policy was changed. Equivalent
profiling commands with an installed `perf` are:

```bash
perf record -e cpu-clock:u -F 199 --call-graph dwarf,8192 -t GAME_PID \
  -o build-logs/main.perf.data -- sleep 20
perf report --stdio --no-inline --no-children --call-graph none \
  -i build-logs/main.perf.data
```

Replace `GAME_PID` with the owned native game's main-thread ID. Disabling inline
symbol expansion avoids a costly `addr2line` pass over the generated executable;
the recorded call chains remain available for caller analysis.

## Deferred texture snapshots

The main-thread profile also attributed 109 memory-move samples to `CopyTexture`.
An eligible deferred copy previously duplicated the same RAM bytes for its
sampling-cache validation and its completion-fence check. These consumers now
share one immutable snapshot. Partial CPU writeback creates a separate snapshot
before changing the expected cache-line bytes; completed readback buffers are
moved into the sampling cache instead of copied again. Exact comparisons, GPU
fences, alias handling and the bounded copy cache remain in place.

`DeferredSnapshotBytes()` counts bytes copied into deferred validation snapshots,
including partial-writeback detachment. It excludes GPU readback payloads and is
not a memory-bandwidth measurement. `SetDeferredSnapshotSharing(false)` is a C++
diagnostic baseline that creates independent snapshots. Both branches use the new
ownership representation, so it is not an exact reproduction of the old binary.
There is no new launcher option; the change affects eligible deferred copies in
the existing experimental performance mode.

The candidate passed 438 synthetic Vulkan checks, 51 Python tests including
native integration, and 21 CTest targets. Coverage includes deferred GPU sampling,
exact materialized pixels, partial/full CPU writeback, aliases and replacement
copies. The one-snapshot assertion first failed against the duplicate-copy code.
The first combined test run failed during Vulkan surface initialization with
`VK_INCOMPLETE`, before the renderer checks; the isolated repeat passed. Both logs
are retained as `stage50-gpu.log` and `stage50-gpu-retry.log` in `build-logs/`.

A same-process visible Fountain Gardens comparison held section 1 at
`(70.038, 24.103)`, with experimental copies, VSync off and muted output. The first
off/on/off series used 20-second frame windows and approximately 29-second
CPU/counter windows:

| Shared snapshots | CPU ms / submission | Snapshot MB / submission | VI submissions/s | p99 interval | Maximum | Intervals over 25 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Off | 11.986 | 5.533 | 56.034 | 34.216 ms | 51.180 ms | 77 |
| On | 10.529 | 2.766 | 55.986 | 61.011 ms | 71.007 ms | 37 |
| Off again | 10.033 | 5.533 | 59.892 | 17.839 ms | 33.368 ms | 1 |

The changing baseline makes that series inconclusive for CPU time and delivery.
A longer on/off/on repeat used 40-second frame windows and approximately
49-second CPU/counter windows:

| Shared snapshots | CPU ms / submission | Snapshot MB / submission | VI submissions/s | p99 interval | Maximum | Intervals over 25 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| On | 10.062 | 2.766 | 59.615 | 18.457 ms | 48.439 ms | 11 |
| Off | 10.470 | 5.526 | 59.169 | 31.765 ms | 70.797 ms | 28 |
| On again | 10.020 | 2.765 | 59.864 | 18.039 ms | 33.606 ms | 3 |

Both enabled phases in the repeat consumed about 4% less main-thread CPU per
submission than the intervening baseline. Snapshot demand halved, and the longer
repeat had fewer long intervals with sharing. This modest improvement supports
retaining the change; the variable first series and remaining stalls prevent a
stable-60 claim. Pipeline counts stayed at zero queued / 302 created, position was
unchanged and no readbacks occurred in any measurement window. The repeat's draw
demand remained about 1,893 draws per submission.

The isolated candidate also reached the level through normal virtual PAD input;
right/left movement and a jump changed observed player coordinates and state.
Native captures before/after the measurements were inspected for visible defects.
These checks do not certify physical controls, moving-gameplay pacing, audio or
Legion Go performance. Evidence: `stage50-snapshots-{comparison,repeat}.json`,
`stage50-snapshots-frames.csv`, `stage50-player.json`, `stage50-tower.png` and
`stage50-enabled-final.png` under ignored `build-logs/`.

The native executable used by the launcher was rebuilt and compared byte-for-byte
with the tested candidate. SHA-256:
`f6a496beb0a76760ba0b90b795e1cd392ce102906e8c050e3dff47f43db26a08`.
The previous executable is retained locally under `local/verified-builds/before-stage50/`.

Verification commands:

```bash
bash tools/container.sh cmake --build build/native \
  --target WiiCompiled kirby-launcher kirby_gx_renderer_test --parallel 3
# Run the Vulkan check separately from other video tests.
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native/kirby_gx_renderer_test --gpu-xfb-hidden
KIRBY_NATIVE_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_NATIVE_VIDEO_TEST="$PWD/build/native/WiiCompiled" \
  KIRBY_LAUNCHER_TEST="$PWD/build/native/kirby-launcher" \
  LD_LIBRARY_PATH="$PWD/local/host-libs" \
  python3 -m unittest discover -s tests -v
bash tools/container.sh ctest --test-dir build/native \
  -R '^(kirby_|mkw_linux_host_context_tests$)' --output-on-failure
```

## Moving gameplay and host audio

The published `d8f602b` native build was exercised for 180.008 seconds in Fountain
Gardens section 1 with a visible window, experimental copies and VSync off. A Linux
uinput gamepad drove repeated direction changes, jumps and yarn-button presses
through SDL. Read-only guest observations at about 10 Hz recorded movement over
x=39.240–77.996 and y=22.380–28.239, including 310 samples in the previously
verified jumping state. No debugger or compilation ran during this interval.
This repeats one part of the level; it is not a level-completion or physical
controller test. The run used an isolated save copy.

The CSV analysis excludes buffered boundary rows, leaving a 173.041-second
measurement with 10,266 samples:

| Native submission metric | Result |
| --- | ---: |
| Mean submission rate | 59.321/s |
| Median interval | 16.691 ms |
| p95 / p99 | 17.593 / 23.935 ms |
| Maximum interval | 69.262 ms |
| Intervals over 25 ms | 102 |
| GPU readbacks | 843 |

The longest interval contained 61.911 ms between submissions and 7.081 ms waiting
for the preceding render worker. It added no readback. Readbacks occurred elsewhere
in the moving run, unlike the stationary snapshot comparisons. The pipeline queue
was empty at CSV sampling points; that alone does not exclude compilation between
samples. These results still contain visible-pacing risks despite the near-60 mean.

Only the owned game's PulseAudio stream was moved to a dedicated null sink, after
matching its client `application.process.id` and verifying exclusive sink use.
The sink monitor captured 179.960 seconds of stereo 32 kHz S16 PCM through the host
audio path. No microphone or other application's audio was recorded. The capture
has RMS 0.07368 and peak 0.72787 on a full-scale-one basis, with no entirely zero
stereo span lasting 20 ms or more. This does not exclude short clicks or prove
physical speaker output.

The music matched the dump's `files/stream/STRM101.brstm`. Both signals were decoded
to mono 8 kHz; normalized cross-correlation compared 1.5-second windows every five
seconds. The original stream's loop runs from sample 630,784 to 4,187,397 at 32 kHz,
verified against its header and Swiftshine's `snd_StrmFile` layout. Reference times
were unwrapped using that loop length. A linear fit to the 26 of 36 windows with
absolute correlation at least 0.8 measured **1.00001086 source seconds per output
second**, with maximum residual 1.840 ms. All 36 selected matches, including the
lower-correlation windows, were within the same residual bound. The alternate
`STRM101B` reference did not match. This supports correct music tempo in this
moving clip, not a whole-game audio or listening certification.

A separate 30-second main-thread `perf` capture during motion collected 2,605
user-space samples. Memory moves accounted for 8.25%; 113 samples had the immediate
copy caller in `CacheTextureBinding`. The longest profiled interval was 83.636 ms;
its samples included several translated and renderer functions, rather than one
clear culprit. Sampling overhead and the different window mean this is not a
before/after performance comparison. Further diagnosis must distinguish CPU work,
host scheduling and blocking waits during individual stalls before choosing a fix.

The initial and final native captures were inspected. After returning focus to the
game, Start opened the pause menu through SDL/PAD; normal Quit navigation returned
to Grass Land. The temporary controller, recorder and owned audio sink were removed
after the run. User saves were not replaced and the published executable was not
changed by these diagnostics.

Local evidence: `stage51-motion-{input,analysis}.json`, `stage51-motion-frames.csv`,
`stage51-audio-analysis.json`, `stage51-main.perf.data`, `stage51-main-stacks.txt`,
`stage51-profile-intervals.json`, `stage51-motion-final.png` and
`stage51-return-map.png` under ignored `build-logs/`. PCM, decoded references and
the input scripts stay local. Longer play, other levels, physical controls, target
hardware and validated 30/60 modes remain unfinished.

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

The first isolated native timing attempt ended before completing the enabled
interval. A subsequent off/on/off run reached Quilty Square, but its capture
showed a disconnected-controller dialog; it is not a gameplay comparison.
Repeating with a continuously connected virtual controller measured
38.71 / 39.25 / 35.20 VI submissions/s over six-second intervals. Readbacks
remained about 2.00 per presentation in all intervals. The hidden, muted run
does not establish an improvement, visible pacing or audible quality. Evidence:
`stage38-connected-rate.json`, `stage38-connected-debugger.log` and
`stage38-connected-enabled.png` under ignored `build-logs/`.

A breakpoint at the actual pending-copy completion identified a BP `0x48`
token interrupt before the display copy (`stage38-fence.log`). The current
completion contract therefore prevents batching in this scene. Removing that
fence without implementing and auditing CPU visibility would be incorrect.
The subsequent cache-boundary build below retains synchronous copies by default.

## Audited CPU cache boundaries (2026-11-13)

Kirby's five SDK invalidate/flush/store range functions now have native
boundaries, pinned by address, symbol, size and SHA-256 in
`tools/audits/cache.py`. The two anonymous store functions were identified from
their cache instruction and the corresponding sequence in Swiftshine's
`src/revolution/OS/OSCache.c`. The boundary retains CR0/SO, GPR3–5, CTR and
system-call behavior and notifies the renderer using the whole 32-byte cache
line range. Zero-length requests do not notify it. Address arithmetic retains
PPC wrapping behavior; an observer handles a wrapped span conservatively.

An overlapping pending GPU copy completes before the cache operation returns.
Unrelated ranges remain queued. MEM1/MEM2 physical, cached and uncached addresses
use the same physical-range comparison. This connects the deferred-copy API to
real guest calls; it does not yet permit dropping the token/finish fences or
enable deferred copies by default. Unbracketed CPU accesses and other GPU RAM
consumers still require an audit before broader GPU-resident behavior.

The cache unit test compares 3,360 cases against the five locally translated
SDK functions, including all byte alignments, zero/nonzero sizes, both SO values,
and cached/uncached address variants. GPU tests cover nonoverlapping and empty
ranges plus an overlapping uncached alias. Both regressions were observed to
fail before implementation. No disc-derived function bodies are checked in;
the oracle functions remain under ignored `generated/`.

The updated game executable started by the launcher has build ID
`5dc2db22b192105bf43154bfb2ad5f5710995330` and SHA-256
`9aebec32349715ebb424bd5420f9c184aed844ced1a05168efd15f156bfebaef`.
Full translation updated 42 functions; the runtime passed 41 Python integration
tests, 21 CTest targets (including the cache ABI comparisons), and 383 synthetic
Vulkan checks. An isolated native run reached Quilty Square with virtual PAD
connected (`stage38-launcher-scene.png`). The published executable was compared
byte-for-byte with that candidate. The ordinary save hash remained unchanged.
Fountain Gardens and audible quality were not rechecked in this build.
Logs: `stage38-translate.log`, `stage38-native-build-final.log`,
`stage38-tests.log`, `stage38-ctest.log`, `stage38-range-green.log`, and
`stage38-launcher-route.log` under ignored `build-logs/`.

## Experimental GPU-resident texture copies (2026-11-13)

The launcher now offers **Experimental performance mode**. The native argument
is `--gpu-resident-copies`; both defaults remain off. The launcher stores the
selection after the existing path, volume and VSync values, so old three-line
settings files retain their existing choices. The user's current local setting
was enabled on request for testing. Changes apply at the next launch.

This mode keeps eligible packed RGB8-to-RGBA8 copies GPU-authoritative across
logical token/finish completion. Display copies still download YUYV to RAM.
CPU invalidation materializes copied bytes on demand. Full CPU writeback or a
complete packed GPU overwrite retires the obsolete copy without downloading it.
Partial CPU writeback materializes the remaining pixels and restores the CPU's
published cache-line bytes. Display-list reads, indexed XF/vertex reads, palette
loads, aliased texture sampling and overlapping display destinations complete
pending copies before consuming RAM. Copies that do not qualify retain the
synchronous path. Queues and caches remain bounded.

Two same-process off/on/off comparisons used Quilty Square, a continuously
connected virtual PAD, a hidden window, muted output and no VSync:

| Interval duration | Mode off | Mode on | Mode off again |
| --- | ---: | ---: | ---: |
| 6 seconds | 37.50 | 46.41 | 40.77 |
| 10 seconds | 39.94 | 41.93 | 37.21 |

Values are VI submissions/s, not compositor-displayed FPS. Readbacks per
presentation changed from approximately **2 to 1**, returning to 2 when disabled.
The increase was present in both comparisons, but its magnitude varied; this is
not a sustained frame-pacing or 60 FPS result. Captures showed the normal scene
without a controller-disconnection dialog. Evidence: `stage39-resident-rate.json`,
`stage39-repeat-rate.json`, their debugger logs and enabled captures in ignored
`build-logs/`.

With the mode enabled, an isolated native run reached Grass Land and Fountain
Gardens. Read-only player observations after virtual input were start
`(15.85,20.925)`, right `(18.53,20.925)`, left `(16.05,20.925)` and jump
`(15.976,23.135)`, with the jump changing player state from 1 to 2. Captures and
logs are `stage39-grassland*`, `stage39-fountain*`. This did not complete the level
or validate a physical controller or audible output.

The published executable has build ID `2bbb614f48e819cb22a5d8022bb72dd27ca80742`
and SHA-256 `0fb0a6b20982edeeabac13b64d08b8d9b4f60f553a7b6ac12948f132bb04a8cd`.
It passed 42 Python tests, 21 CTest targets and 398 synthetic Vulkan checks.
The new CLI option passed startup/exit verification. The launcher and game
executables were each compared byte-for-byte against their tested candidates.
Logs: `stage39-final-{build,tests,ctest}.log`,
`stage39-resident-{red,green}.log`, and `stage39-publish-build.log`.

Compatibility remains experimental. Cached CPU reads must use the audited
invalidate boundary to obtain GPU bytes; raw CPU reads, uncached accesses and
external DMA writes are not intercepted by this implementation. Unexpected RAM
writes outside an explicit writeback range fail at materialization rather than
silently being overwritten. Broader scene/consumer coverage, visible pacing and
listening tests are required before enabling this mode by default.

## Audio alignment and VI pacing (2026-11-13)

The user still reports image stutter with experimental mode enabled. A ten-second
hidden Quilty Square sample, with that mode enabled and VSync off, observed
46.90 VI submissions/s. Periods between counter increments were median **17.22 ms**,
p95 **35.43 ms**, p99 **39.43 ms**, and maximum **67.41 ms**. This is uneven delivery
despite a higher average rate. It is not a 60 FPS result.

The sampler read the renderer's submission counter through `/proc/PID/mem` about
once per millisecond without stopping the process during the interval. No counter
increments were missed. The first interval was discarded, and a one-second
settling period followed the initial debugger attachment used to locate the
object. Values have polling/scheduling uncertainty and describe VI submissions,
not physical display scanout. Evidence: `stage40-frame-periods.json` and
`stage40-frame-object.log` under ignored `build-logs/`.

For a separate audio check, a temporary bounded trace captured Wii AI DMA bytes
before host gain. The eight-second Quilty Square clip was compared with the
player's `files/stream/TOWN1.brstm`, decoded by FFmpeg. Both signals were reduced
to mono at 8 kHz for normalized cross-correlation of 0.75-second windows spaced
one second apart. Matches advanced by exactly one source second per captured
audio second; correlations ranged from **0.959 to 0.990**. This establishes the
correct musical progression in this clip, rather than merely the expected number
of mixer callbacks. It does not establish continuous wall-clock delivery.

A second capture tested the SDL/PipeWire output. Only the isolated test game's
stream was routed to a temporary null sink, identified through its PipeWire
client PID. Its monitor supplied 7.168 seconds of PCM. Matching windows again
advanced at **1.000 source seconds per output second**, with correlations from
**0.995 to 0.998**. The stream was restored and the temporary sink removed. This
includes host resampling but excludes the physical audio device and speakers.
No microphone or other application's audio was captured.

The earlier file-selection confirmation capture contained only zeros before host
gain. Its expected behavior still requires a reference check; it must not be
used to claim that all menu audio works. Other scenes, long-term synchronization
and physical listening remain unverified.

Evidence: `stage40-town-correlation.json`, `stage40-host-correlation.json`,
`stage40-host-audio.log`, `stage40-audio-profile-game.log`, and the corresponding
scene captures in `build-logs/`. PCM and decoded references stay under ignored
`local/`. The trace changes were removed from the source after measurement;
they were never published to the launcher. At this stage, the published build
remained the Stage 39 build identified above. Display-copy GPU readback and
frame submission scheduling were the next pacing targets.

## Direct GPU display copies (2026-11-13)

Experimental performance mode now also keeps display copies on the GPU until
VI selects them. Normal presentation no longer downloads a full YUYV image to
guest RAM first. Each destination retains its own GPU image and generation;
post-copy EFB clears do not change a previously copied image. CPU invalidation,
RAM-consuming graphics commands and disabling this path materialize pending
XFB bytes. Replacing the same complete XFB retires the obsolete image without
downloading it. Different overlapping layouts use the synchronous fallback.

The same-process Quilty Square comparison kept GPU-resident **textures** enabled
throughout and toggled only resident **display** copies. It used a hidden window,
connected virtual PAD, muted output and VSync off. Each interval followed two
seconds of settling and sampled VI submissions for ten seconds:

| Display copies | VI submissions/s | Median interval | p95 | p99 | Maximum | Readbacks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Synchronous | 54.20 | 17.04 ms | 33.45 ms | 35.26 ms | 37.75 ms | 542 |
| GPU resident | 59.90 | 16.99 ms | 19.31 ms | 22.44 ms | 34.53 ms | 0 |
| Synchronous again | 54.40 | 17.03 ms | 33.63 ms | 35.18 ms | 36.70 ms | 544 |

Polling used `/proc/PID/mem` at approximately 1 ms intervals, with no debugger
stops during sampling and no missed counter increments. The first partial
interval was discarded. These results support reduced submission stalls in this
scene; they do not certify visible 60 FPS, original gameplay speed, physical
audio output or Legion Go performance. Outliers remain. Evidence is in ignored
`build-logs/stage41-frame-periods.json`, `stage41-benchmark.log` and
`stage41-toggle-{0,1,2}.log`.

The GPU regression first failed at the new no-readback assertion, then passed
415 checks after implementation. Coverage includes independent destinations,
pre/post-clear pixels, RAM invalidation through aliases, complete replacement,
overlapping-view fallback, unexpected CPU writes and alias-boundary wrapping.
The candidate also passed 42 Python tests and 21 CTest targets, including native
startup with the option enabled. Logs: `stage41-display-{red,final}.log`,
`stage41-final-build.log`, `stage41-python.log` and `stage41-ctest.log`.

With the option enabled from startup, an isolated save reached Quilty Square,
Grass Land and Fountain Gardens. Virtual PAD input moved Kirby from
`(15.85,20.925)` to `(18.61,20.925)` right and `(16.13,20.925)` left; jumping reached
`(16.056,23.000)` and changed player state from 1 to 2. Captures explicitly
materialized XFB RAM once, outside measurements. Evidence: `stage41-startup*`,
`stage41-grassland*`, `stage41-door*`, `stage41-fountain*` and `stage41-player.json`.
This does not validate a physical controller or complete the level.

The launcher now starts the verified candidate, compared byte-for-byte after
publication. Build ID: `6bce22b6758614369d77702c5565692f6e697097`;
SHA-256: `288aeabc66ec1e7d0534a602a166040f081f02ca1a7a8fe6e4f8cc82cb8c9b10`.
The launcher binary itself is unchanged. Existing saved experimental-mode
selection applies on the next game launch. The ordinary save hash stayed
`fc034a13879ec25699424fb95758b4e32ee8ef4861083bbe5814a7553dc1767e`.
Publication commands and results are in `stage41-publish-{configure,build}.log`.

The existing CPU-consumer limitation remains: raw CPU reads, uncached accesses
and external DMA are not intercepted. CPU-modified XFB upload remains unsupported
and fails explicitly. Full-game compatibility and visible pacing need further
validation; the option remains experimental and defaults off. This change does
not alter the guest timebase, audio rate or introduce a 30/60 FPS selector.

## Texture hashing investigation and candidate regression (2026-11-13)

A warm Fountain Gardens section-1 run near the tower sampled the guest thread
inside `hash_texture_source` / XXH3 in 17 of 30 stack samples. In a separate
same-process off/on/off experiment, Aurora's created-pipeline count remained
302 and its queued-pipeline count remained zero throughout each ten-second
interval. No display readbacks occurred. This identifies repeated CPU texture
hashing as a measured cost in this scene; it does not exclude first-use shader
compilation elsewhere. Evidence: ignored `stage42-tower-stack-samples.json`,
`stage42-stacks.log`, `stage42-object-periods.json` and
`stage42-object-benchmark.log` under `build-logs/`.

The candidate supplies stable Aurora texture identities only after comparing
the current source bytes and palette against bounded snapshots. An initial
Fountain Gardens test revealed stretched geometry with this optimization
enabled. The off/on/off captures (`stage42-door-{off,on,off-again}.png`) reproduce
the regression. Its throughput measurements are **not accepted performance
results**, because incorrect draw merging can itself reduce work.

Previously, unconditional texture invalidation also prevented Aurora from
merging consecutive draws. Removing it exposed a missing indexed-array
invalidation: a draw with changed guest vertex data could retain the preceding
draw's GPU array offsets. The candidate now marks immediate data dirty whenever
an indexed array needs a new upload, including a larger referenced prefix.

A regression draws off-screen indexed positions, changes the same guest RAM to
on-screen positions, and draws again without an intervening BP/CP command or
frame boundary. Before the fix, the second draw incorrectly disappears; the
saved failing executable stops at check 227. After the fix, all 423 Vulkan XFB
checks pass. Commands and logs:

```bash
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  local/stage43-array-red-test --gpu-xfb-hidden
# Expected failure: consecutive draws with changed indexed RAM.
LD_LIBRARY_PATH="$PWD/local/host-libs" \
  build/native-candidate/kirby_gx_renderer_test --gpu-xfb-hidden
# 423 Vulkan XFB checks passed.
```

The corrected candidate also passes 21 CTest targets and all 42 Python checks,
including native video startup and launcher integration. Logs:
`stage43-array-{red,green}.log`, `stage43-array-green-build.log`,
`stage43-ctest.log` and `stage43-python.log`.

An isolated native run reached Quilty Square, Grass Land and Fountain Gardens.
Virtual PAD input moved Kirby from x=15.85 to 18.61 right, then to 15.97 left;
jumping raised y from 20.925 to 23.135 and changed player state from 1 to 2.
At the tower door, x=76.027/y=24.925, the corrected off/on/off captures no longer
show the stretched enemy, HUD triangles or yellow line from the failed candidate.
Evidence: `stage43-real-player.json`, `stage43-object-door.json`,
`stage43-object-tower-door.png`, `stage43-door-{off,on,off-again}.png` and
`stage43-visual-compare.log`. This is a bounded visual check, not full-game
compatibility or physical-controller validation.

A new same-process comparison near the tower used the corrected candidate,
hidden Vulkan window, muted output and no VSync on the Acer/RTX 3050 test host:

| Texture snapshots | VI submissions/s | Median interval | p95 | p99 |
| --- | ---: | ---: | ---: | ---: |
| Disabled | 21.80 | 47.89 ms | 69.35 ms | 76.54 ms |
| Enabled | 25.00 | 36.44 ms | 60.82 ms | 65.73 ms |
| Disabled again | 21.30 | 49.71 ms | 68.70 ms | 74.19 ms |

Each phase lasted ten seconds after two seconds of settling. Stage/resource/
section stayed `(1,101,1)`, no counter increments were missed by the 1 ms polling,
and readbacks stayed zero. Queued/created pipeline counts stayed `(0,302)`.
The first partial interval was discarded. Evidence:
`stage43-object-periods.json` and `stage43-object-benchmark.log`. This is a modest
submission-throughput improvement with substantial remaining stalls, not stable
30/60 FPS, physical scanout timing or Legion Go performance. Neither the guest
clock nor the audio rate changes, and there is no new 30/60 FPS setting.

The verified candidate was published to `build/native/WiiCompiled` and the
launcher, with byte-for-byte comparisons against both candidate executables.
Build ID: `2b813afe91009ffeca47404b025bd58a8bc95532`;
SHA-256: `80f6cfd9e5ca9a0eb00c6e82784bae5f5f14cd975c3c6d158b72748b8bf02736`.
The existing experimental-performance checkbox enables texture snapshots on
the next launch. Publication logs: `stage43-publish-{configure,build}.log`.

## Window recording validation (2026-11-13)

Two X11 recordings were rejected: captured timestamps clustered in bursts and
introduced pauses beyond those attributable to the game. The replacement uses
PipeWire capture of the native Wayland game window, with an isolated game-only
audio sink and ordinary automated keyboard input. No debugger runs during the
take. The inspected 35-second source segment contains 2,145 captured frames;
capture intervals have a 14 ms median, 21 ms p95 and 35 ms maximum, with no gaps
over 50 ms. These are capture timings, not a whole-game rendering benchmark.

The local MP4 trims source seconds 1–36 and removes the title bar. It contains
704×528 H.264 video and the same take's AAC audio (mono, 44.1 kHz), with a 35-second
duration and fast-start metadata. Export samples at 60 frames/s by holding or
selecting captured frames; it performs no motion interpolation, time stretching,
pitch adjustment or replacement-audio editing. The file decodes without errors;
audio measures -24.4 dB mean and -4.7 dB peak. Movement and jump observations,
capture timings and export checks are in ignored `build-logs/stage44-record-*`
and `stage44-export-*`; recordings remain outside Git. The tower's performance
limitations documented above still apply.

## Synchronous batch validation (2026-11-13)

The NSMBWiicompiled source review reinforced the need to preserve texture binding
and copy coherence when removing repeated work (see [references](references.md)).
The implementation here is specific to Kirby's synchronous FIFO decoder; no NSMB
source, SDK addresses or memory layouts were imported.

A new 30-sample investigation near the Fountain Gardens tower found the main
thread comparing texture bytes in 18 samples (15 in `CacheTextureBinding`, three
in `ValidateCopyTexture`). The render worker was waiting in 29 samples. A separate,
intrusive 2,000-binding trace found 1,731 repeated pointer/size bindings within the
same `Feed` call: 373,046,112 of 381,420,928 requested bytes were repeated. This
trace identifies redundant work, not a timing measurement or a complete cache key.

Guest code does not execute between draws decoded by one synchronous `Feed` call.
The renderer now reuses byte-validation results within that batch, while still
matching the full texture/palette descriptor. A new batch, renderer RAM write or
palette reload invalidates the results. Copied GPU textures follow the same rule.
The existing exact comparisons remain at each new boundary; raw CPU changes
between batches do not require a BP rebind or a cache-maintenance instruction.
This relies on the current single guest execution thread; moving guest execution
or memory-writing services onto concurrent threads would require revisiting it.

The first display-list regression failed at check 208 before the optimization.
The final candidate passes 430 Vulkan checks, including repeated display-list
draws, the per-draw diagnostic baseline, a palette reload inside one batch and a
GPU readback overwriting texture indices before the next draw in that batch.
All 21 CTest targets and 42 Python checks passed, including native startup and
launcher integration. Logs are under ignored `build-logs/stage46-*`.

Same-process off/on/off measurements used the Acer/RTX 3050 host, resident copies,
muted audio, no VSync, ten seconds per sample and two seconds of settling. The
switch controls only batch validation; source snapshots remain enabled throughout.

| Window / scene | Batch validation | VI submissions/s | Median interval | p95 | p99 |
| --- | --- | ---: | ---: | ---: | ---: |
| Hidden, tower | Off | 24.10 | 39.98 ms | 63.28 ms | 76.36 ms |
| Hidden, tower | On | 58.70 | 16.97 ms | 19.31 ms | 33.74 ms |
| Hidden, tower | Off again | 24.30 | 39.98 ms | 61.22 ms | 73.51 ms |
| Visible, later section-1 position | Off | 30.50 | 32.42 ms | 58.35 ms | 69.81 ms |
| Visible, later section-1 position | On | 54.10 | 17.13 ms | 33.59 ms | 38.35 ms |
| Visible, later section-1 position | Off again | 30.30 | 33.42 ms | 52.75 ms | 66.64 ms |

The accepted visible series held Kirby at x=190.103/y=32.425 before and after
every interval. Stage/resource/section remained `(1,101,1)`. Created pipelines
stayed at 302 for the hidden series and 320 for the accepted visible series;
queues stayed empty and no readbacks occurred during these intervals. An earlier
visible series is excluded: the scene changed, readbacks occurred and new pipelines
were created after showing the window. Do not compare its changing scene to the tower.

Static texture validation demand fell from about 467 MB to 8.4 MB per submission
in the hidden series, and from about 356 MB to 12.6 MB in the visible series.
Matching off/on/off tower framebuffer captures were inspected for visible
regressions. Later visible captures contain movement and are not an identical-scene
image comparison.
These are submission intervals, not compositor frame delivery or audio-rate tests.
The visible p95 still exceeds 33 ms; stable whole-game 60 FPS is not established.
The earlier resident-copy CPU visibility limitations remain unchanged.

Local evidence and commands:

```bash
python3 local/stage46-object-benchmark.py
python3 local/stage46-visible-warm-benchmark.py
# Each script requires its isolated native session metadata and verified scene.
# Results: build-logs/stage46-object-periods.json
#          build-logs/stage46-visible-warm-periods.json
```

Raw traces, scene captures and session scripts remain ignored. The optimization
uses the launcher's existing Experimental performance mode and does not add an
FPS selector or interpolation.

The tested candidate was installed at `build/native/WiiCompiled` and compared
byte-for-byte with the executable used above. SHA-256:
`8c7e0bef9b39f81bb735a10eea2887e0df65ee944a202a5aff67a8f143a74ef2`.
The launcher uses that executable on its next launch.

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
