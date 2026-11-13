# Changelog

## Unreleased

- Reuse Aurora texture identities after exact source-byte validation, reducing
  repeated texture hashing while preserving raw CPU edits and palette changes.
- Present GPU-resident display copies directly in experimental performance mode,
  with on-demand XFB RAM materialization and a synchronous overlapping-view fallback.
- Keep frame-rate selection separate from performance optimization; validated
  30/60 FPS modes remain pending.
- Add an opt-in **Experimental performance mode** to the launcher and native
  `--gpu-resident-copies` argument. Copied textures stay on the GPU until CPU
  invalidation or a RAM-consuming graphics command requires their bytes.
- Distinguish cache invalidation from CPU writeback, including full replacement
  and partial writes that preserve the remaining GPU pixels.
- Add five audited native SDK cache boundaries, preserving register effects and
  completing overlapping pending GPU copies across guest memory aliases.
- Add experimental batched texture readback with completion/alias tests. It stays
  disabled by default: native token fences prevented a measured batching benefit.
- Reuse packed RGB8-to-RGBA8 GPU copy textures while preserving RAM readback;
  invalidate direct sampling after CPU writes or texture descriptor changes.

- Interleave overdue native audio buffers with their original AID/DSP callbacks,
  preventing repeated consumption of stale sample buffers after host stalls.
- Deliver queued hardware IRQs when the audited SDK interrupt boundary enables EE.
- Preserve per-thread IRQ continuations and the original preempted register save.
- GPU YUYV display-copy packing halves XFB readback bytes and removes the measured
  CPU color-conversion pass while retaining guest RAM visibility.
- Game-window FPS counter uses Aurora's completed surface presentations.
- Hidden startup tests explicitly set volume to zero.
- Native Linux launcher with revision verification, game launch, master volume,
  VSync selection, detected controllers and access to saves/logs.
- Reuse indexed GPU uploads for unchanged RAM, including redundant CP bindings.
- Launcher smoke/verification tests and native volume argument validation.
- Fixed silent MEM2 voices: the AX mixer now maps the configured 64 MiB retail
  extent instead of requiring the runtime's 128 MiB NDEV allocation.
- README reorganized around playing and known limitations, with focused guides.
- Live FPS and PCM findings recorded; stable 30/60 FPS and listening/synchronization checks remain unfinished.

## Earlier native bring-up

- Fountain Gardens loading, movement and jumping verified through virtual PAD.
- Patch Castle completion, save commit and independent progression reload.
- Standard/PS5/Xbox SDL fixtures and positional Xbox face-button defaults.
- Host exit handling and corrected audio/SDL destruction order.
- Boot memory, clocks, SRAM, SI, IOS, disc and NAND support needed by observed gameplay.

Commands and evidence: [bring-up results](docs/bringup.md).
