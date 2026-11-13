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
