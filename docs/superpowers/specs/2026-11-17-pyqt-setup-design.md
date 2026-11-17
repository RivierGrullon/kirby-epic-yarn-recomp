# Independent PyQt setup

The user chose PyQt for a separate graphical installer. Reuse the existing
JSON-lines worker, verified tools downloads, native build checks and atomic
installation selection. The launcher should concentrate on playing and settings.

Use PyQt6 QtWidgets and QProcess. The form selects a supported own disc image or
DATA directory and an installation folder (default `~/Games/Kirby Native`). Install
prepares a versioned private project under `<destination>/.kirby/<revision>/`, runs
the worker there and shows stage, measured progress, elapsed time, a step list and
expandable details. Failures allow retry; cancellation and closing the window wait
for worker cleanup. Completion is accepted only after a valid committed result and
successful process exit. Provide Open launcher and Open folder after success.

Keep authored source snapshots pinned in the installer instead of adding a second
mutable source download. Reuse the separate tools archive and shared cache from the
compact-AppImage work. The installer bundles Python and Qt. It copies the native
launcher and necessary host libraries to the installation, so playing does not
depend on the installer remaining mounted. Generated code, tools and saves are
never taken from the developer checkout. Existing installations are preserved.
Different source revisions remain in separate directories; save migration across
revisions remains outside this change, as in the existing AppImage.

The destination contains a simple play shortcut pointing at the verified version.
Write it only after installation succeeds. Do not embed game files in release
artifacts. Use the worker's installation record to choose the game data on first
launcher start; keep saved settings on subsequent runs.

The Linux AppImage opens Kirby Setup directly, with a frozen Qt frontend and a
separate console worker. Prepare the Windows application folder using native
PyInstaller and the project's existing native Windows build tools; do not claim a
Windows executable was built or tested on Linux. Preserve headless worker commands
and old AppImage compatibility. The launcher's Install page links to Kirby Setup
instead of starting installation jobs itself.

Visual direction: inherit the existing navy, warm white and raspberry palette,
system sans typography, normal Qt keyboard navigation and accessible contrast.
Use a single window with form, progress and final actions, without decorative
cards or additional modal steps. All slow worker actions run outside the UI thread.

Alternatives considered: keep the current ImGui installation page, or use Tkinter
as the reference does. The user selected a standalone PyQt frontend. Minor choices
above (versioned destination layout, pinned bundled sources and default folder)
are implementation decisions made to preserve existing worker contracts.

Validation: stdlib event model and destination preparation tests, real QProcess
success/failure/crash/cancellation tests, offscreen UI tests and captures at two
sizes, full tooling suite, frozen Qt/worker startup and a packaged Linux AppImage
smoke check. Full game compilation and Windows runtime testing must be reported
separately from GUI and packaging checks.
