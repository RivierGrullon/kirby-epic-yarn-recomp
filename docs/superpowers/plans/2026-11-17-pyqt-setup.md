# Independent PyQt Setup Implementation Plan

**Goal:** A separately packaged PyQt installer that drives the existing worker.
**Spec:** `docs/superpowers/specs/2026-11-17-pyqt-setup-design.md`
**Architecture:** stdlib session model and installation layout, QProcess controller,
QtWidgets frontend, independent frozen application, launcher handoff.

### Task 1: Session protocol and destination layout

Create `tools/installer/setup_model.py`, `setup_layout.py` and tests. Verify strict
identity, sequence, bounded JSON, progress units, committed result, exit status,
source matching, retries and preservation of existing destinations/saves. Prepare
only authored snapshots and launcher libraries. Install shortcuts after success.

### Task 2: PyQt frontend

Create `tools/setup_gui.py`, `tools/installer/setup_gui.py`, pinned Qt requirements
and offscreen tests. Use QProcess with literal arguments; cancellation is a worker
request followed by bounded escalation. Show file/folder chooser, destination,
measured/indeterminate progress, elapsed time, steps, details, retry and launch.
Preserve UI responsiveness, keyboard navigation and source/destination state.

### Task 3: Packaging and launcher handoff

Adapt AppDir/AppImage entry points; add Qt packaging container and Windows freezer.
Copy GUI, worker, CA bundle and Qt notices, excluding build tools. Replace launcher
installation controls with a handoff to setup. Preserve CLI diagnostics.

### Task 4: Verification

Run new tests red then green, the whole tooling suite and native launcher checks.
Review with an independent agent; capture form/running/error/completed views at
desktop and compact sizes. Build and measure an actual Qt AppImage, test frozen
worker and UI without host Python/Qt, then leave a reviewable local commit.
Do not publish release artifacts automatically.
