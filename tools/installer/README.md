# Setup worker

`python3 tools/install.py` reads version 1 JSON-lines requests on stdin and writes
only JSON-lines events to stdout. Run it on the host, with Dolphin's CLI or the
Dolphin Flatpak and 7z installed. Python uses only its standard library.

One worker handles one inspection. The launcher must keep draining stdout and
poll the process exit status. It may keep stdin open until the worker exits.
`--inspect PATH` starts the same operation without a stdin request;
`--workspace PATH` changes the default `local/setup` workspace.

```json
{"version":1,"id":"example-1","operation":"inspect","source":"/path/to/own-disc.wbfs"}
```

Cancellation uses the same job ID on the worker's open stdin:

```json
{"version":1,"id":"example-1","operation":"cancel"}
```

IDs contain 1–80 ASCII letters, digits, underscores or hyphens. Requests are
limited to 64 KiB per line. Unsupported operations, including `install`,
`rebuild` and `repair`, fail explicitly. Inspection alone is not an installed
or successfully compiled game.

Events include `version`, `job_id`, a monotonically increasing `sequence`,
`stage` and `state`. Stages are `inspect`, `hash`, `unpack`, `extract`,
`validate` and `request`. The final `inspect` event has `complete`, `failed` or
`cancelled` state. The process exits with 0, 1 or 2 respectively. Active control
requests other than cancellation are rejected without starting another job.

Hash events include real `completed` and `total` byte counts. Unpack/export
events carry no invented percentage. A started inspection includes `log_path`;
the log contains external tool output, bounded to 64 MiB. Paths and errors are
JSON strings, not shell commands. Invoke the worker and tools with argument
arrays. Tools receive no control-channel stdin and run in owned process groups.

A successful result contains:

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
by Git. This worker currently runs extraction tools only: owning container build
lifetimes, translation/build manifests and atomic executable selection remain
part of the subsequent installation stages.
