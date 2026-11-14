# Setup worker

`python3 tools/install.py` reads version 1 JSON-lines requests on stdin and writes
only JSON-lines events to stdout. Run it on the host, with Dolphin's CLI or the
Dolphin Flatpak and 7z installed. Python uses only its standard library.

One worker handles one inspection or installation. The launcher must keep draining stdout and
poll the process exit status. It may keep stdin open until the worker exits.
`--inspect PATH` or `--install PATH` starts the corresponding operation without a stdin request;
`--workspace PATH` changes the default `local/setup` workspace.

The native launcher uses this worker from its Install page. Its asynchronous
client lives in `launcher/setup/`: it validates event IDs, ordering, types and
the supported revision, bounds line storage and per-frame reads, and accepts a
result only after exit code 0 and a valid final event. Import failure/cancellation
does not change the selected data. The native `--check-source PATH` command runs
the same client without opening the launcher or saving settings.

```json
{"version":1,"id":"example-1","operation":"inspect","source":"/path/to/own-disc.wbfs"}
```

Cancellation uses the same job ID on the worker's open stdin:

```json
{"version":1,"id":"example-1","operation":"cancel"}
```

IDs contain 1–80 ASCII letters, digits, underscores or hyphens. Requests are
limited to 64 KiB per line. `install` accepts the same source field as `inspect`.
Other operations, including `rebuild` and `repair`, fail explicitly. Inspection alone is not an installed
or successfully compiled game.

Events include `version`, `job_id`, a monotonically increasing `sequence`,
`stage` and `state`. Stages are `inspect`, `hash`, `unpack`, `extract`,
`validate` and `request`; installation additionally uses `prepare`, `translate`,
`compile`, `verify`, `publish` and `select`. The final event uses the requested
operation as its stage and has `complete`, `failed` or
`cancelled` state. The process exits with 0, 1 or 2 respectively. Active control
requests other than cancellation are rejected without starting another job.

Hash events include real `completed` and `total` byte counts. Unpack/export
events carry no invented percentage. A started inspection includes `log_path`;
the log contains external tool output, bounded to 64 MiB. Paths and errors are
JSON strings, not shell commands. Invoke the worker and tools with argument
arrays. Tools receive no control-channel stdin and run in owned process groups.

A successful inspection result contains:

| Field | Meaning |
| --- | --- |
| `source`, `source_kind` | Actual selected source and its type |
| `data_root` | Complete verified DATA directory to use in later stages |
| `game_id`, `revision`, `dol_sha1`, `entry_point` | Validated executable/revision identity |
| `asset_files`, `asset_bytes` | Files and bytes declared in the validated FST |
| `validation` | `dol-sha1-and-data-layout`; not full asset-content authentication |
| `source_sha256`, `cached` | Image/archive content identity and reuse flag; absent for directories |

Directory inspection is read-only and does not consult this checkout's `disc/`.
Images and archives are hashed, exported under `staging/`, and checked against
the native boot layout before an atomic rename publishes `sources/SHA256/`.
An archive unpacks only its literal WBFS member and checks the selected payload.
The remaining archive members are not imported. Reuse requires both the same
selected-source hash and a successful DATA layout check; a damaged cache fails
instead of silently accepting another extraction.

`source.json` records the source hash and byte count. It is a local cache record,
not a signed installation manifest or a native build identity. Folder inspection
checks metadata/layout at that moment; it does not snapshot the folder. Later
install stages must revalidate or copy their selected data before publishing.

The workspace lock prevents concurrent setup jobs. Failed or cancelled work
removes only its temporary directory; existing caches, `disc/`, executables,
settings and saves remain intact. Raw game data and logs stay local and ignored
by Git. An installation may retain compiler caches after failure or cancellation.
It never overwrites `build/native`, user settings or saves.

## Native installation

Prepare the pinned dependencies and local toolchain image as described in
[Building](../../docs/building.md), then run on the host:

```bash
python3 tools/install.py --install "/path/to/extracted/DATA"
```

The worker inspects the selected source, checks pinned dependencies, resolves
the toolchain image to an immutable ID, copies and rechecks the DOL, translates,
and builds in an owned cache under `local/setup/builds/cache-SHA256/`. Compile
parallelism is bounded by available RAM and CPUs, up to four jobs. Translation
receipts hash their generated files; native receipts hash the executable and
required DSP coefficient resource. Changed or damaged inputs invalidate reuse.

Verification runs on an independent copy: Linux x86-64 ELF and build provenance,
bootstrap, SDK initialization, disc access, ES metadata, and a hidden first-frame
probe with initialized controller/audio services. Probes use dummy audio, zero
volume and temporary user data. They do not certify visible smoothness, audible
sync, Fountain Gardens gameplay or whole-game compatibility.

Only a verified bundle is copied to `local/setup/installations/SHA256/`, with a
manifest identifying the source, checkout, toolchain and artifact hashes. The
worker then atomically replaces `local/user/installation.json`, retaining the
previous ID for rollback. Mutable compiler outputs are never the selected copy.
Cancellation before selection preserves the old pointer. Once selection commits,
the terminal result reports `committed: true` even if a cancellation arrives late.
The install result also includes `installation_id`, `executable`, `data_root`,
`translation_reused` and `build_reused`.

The launcher offers **Build / update native game** for its selected DATA folder.
Its asynchronous client accepts a committed install result only after a valid
final event and exit 0, checking the selected source and expected installation
path. It then rechecks the on-disk manifest and artifact hashes before using the
new executable. A late cancellation cannot be reported as preserving the old
selection after the worker has already committed. Build progress and cancellation
remain available during translation/compilation; Play is disabled during that
launcher's setup job. Closing waits for cancellation.

On startup the launcher reads the active installation pointer, validates its
manifest identity and hashes the executable and DSP resource. It uses its sibling
developer executable only when no installation pointer exists. Broken installed
metadata/artifacts block Play. The reader reuses Crypto++ from the pinned runtime;
upstream source and license files remain intact. These hashes detect changes;
they are not signatures from a trusted distributor.

The worker requires an existing toolchain image; automatic toolchain preparation,
dedicated repair/rollback controls and cross-process Play/build locking remain
pending. Repeating `--install` reuses matching compiler caches and rebuilds damaged
compiler outputs. It does not yet repair a corrupted immutable installation.

## Managed build containers

`installer.container.run_container` runs build tools through the same container
wrapper as developer commands. It resolves the local toolchain tag to an
immutable image ID, records ownership before starting, and applies both a job
label and a unique per-run token. Commands remain argument arrays.

Stopping the Podman client alone is insufficient to prove that its container
and compiler children stopped. After success, failure or cancellation, the
runner force-removes only containers matching both ownership labels and queries
the engine again to verify absence. Cleanup ignores the cancellation flag and
has its own deadlines and bounded output. CID files provide runtime information;
they are not trusted as removal targets.

If cleanup cannot be verified, `CleanupError` retains the private ownership
receipt under `staging/container-*/ownership.json`. The result is a failure,
not successful cancellation or installation. After acquiring the workspace
lock, a subsequent coordinator can call `recover_containers(job)` to validate
receipts and recheck/remove those exact engine objects. Invalid or linked
receipt metadata is rejected without touching other containers.

The shell interface `container.sh --managed CIDFILE JOB TOKEN IMAGE_ID -- COMMAND`
is internal to this runner. Ordinary `container.sh COMMAND` and `--build` retain
their behavior. The runner does not add build buttons to the frontend by itself.
