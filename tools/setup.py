#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare and install Kirby from your own disc using Docker or Podman."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = os.name == 'nt'


def windows_runtime_missing():
    # The pinned Dawn DLL imports the Microsoft C++ runtime. Check the x64
    # system directory even when setup is running under a 32-bit Python.
    system = Path(os.environ.get('SystemRoot', r'C:\Windows'))/('System32' if sys.maxsize > 2**32 else 'Sysnative')
    names = ('MSVCP140.dll', 'MSVCP140_ATOMIC_WAIT.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')
    return [name for name in names if not (system/name).is_file()]


def preflight(source, requested='auto'):
    if sys.version_info < (3, 11):
        raise ValueError('Python 3.11 or newer is required. See docs/building.md.')
    from container_engine import select_engine
    if sys.platform not in ('linux', 'win32') or platform.machine().lower() not in ('x86_64', 'amd64'):
        raise ValueError('This build targets Windows x86-64 and Linux x86-64.')
    if not source.exists():
        raise ValueError(f'Source not found: {source}. Use the actual path to your disc image or extracted DATA folder; '
                         'the /path/to/... paths in examples are placeholders.')
    if not source.is_dir() and (not source.is_file() or source.suffix.lower() not in ('.wbfs', '.iso', '.rvz', '.7z')):
        raise ValueError('Choose a DATA folder, WBFS, ISO, RVZ, or a .7z containing one WBFS. A .7z is optional.')
    missing = []
    if WINDOWS:
        runtimes = windows_runtime_missing()
        if runtimes:
            missing.append('Microsoft Visual C++ v14 runtime (x64): '+', '.join(runtimes)+
                           '. Install the x64 redistributable from https://aka.ms/vc14/vc_redist.x64.exe')
    if not shutil.which('git'):
        missing.append('Git (install Git and add it to PATH)')
    engine = None
    try:
        engine = select_engine(requested)
    except ValueError as error:
        missing.append(str(error))
    if source.is_file():
        dolphin = shutil.which('DolphinTool.exe') if WINDOWS else shutil.which('dolphin-tool')
        if not dolphin and not WINDOWS and shutil.which('flatpak'):
            dolphin = subprocess.run(['flatpak', 'info', 'org.DolphinEmu.dolphin-emu'],
                                     capture_output=True, timeout=15).returncode == 0
        if not dolphin:
            missing.append("Dolphin's dolphin-tool / DolphinTool.exe, or the installed Dolphin Flatpak "
                           '(only needed to extract disc images; add its folder to PATH)')
        if source.suffix.lower() == '.7z' and not (shutil.which('7z') or shutil.which('7zz')):
            missing.append('7-Zip: 7z or 7zz (only needed for .7z archives)')
    if missing:
        raise ValueError('Missing prerequisites:\n  - '+'\n  - '.join(missing)+'\nSee docs/building.md, then rerun the same command.')
    os_field = '{{.Host.OS}}' if engine == 'podman' else '{{.OSType}}'
    result = subprocess.run([engine, 'info', '--format', os_field], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(f'{engine} is installed but unavailable. Start it and check "{engine} info".\n'
                         + result.stderr.strip()[-2000:])
    if result.stdout.strip().lower() != 'linux':
        raise ValueError('Use Linux containers in Docker Desktop or Podman. The Windows target still produces native .exe files.')
    from installer.inputs import host_compile_jobs
    host_compile_jobs()
    return engine


def progress(event):
    if event.get('state') == 'progress':
        return
    stage = event.get('stage', 'setup').replace('_', ' ')
    state = event.get('state', '')
    detail = f" ({event['check']})" if event.get('check') else ''
    if event.get('reused'):
        detail += ' (using cached build)'
    print(f'{stage.capitalize()}: {state}{detail}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?', type=Path, default=ROOT/'disc/DATA',
                        help='Actual disc image or extracted DATA folder (default: disc/DATA)')
    parser.add_argument('--engine', choices=('auto', 'docker', 'podman'), default='auto')
    parser.add_argument('--check', action='store_true', help='Check prerequisites without downloading or compiling')
    args = parser.parse_args()
    job = None
    previous_signals = {}
    try:
        source = args.source.expanduser().absolute()
        engine = preflight(source, args.engine)
        os.environ['KIRBY_CONTAINER_ENGINE'] = engine
        print(f'Prerequisites ready. Container engine: {engine}.', flush=True)
        if args.check:
            print('The game revision and graphics are checked during installation.')
            return 0
        from container_engine import build_command, image_name
        from installer.container import recover_containers
        from installer.jobs import Job, run_process
        from installer.pipeline import execute_install
        from installer.sources import inspect_source
        with Job(ROOT/'local/setup', 'setup-'+uuid.uuid4().hex, progress) as job:
            # Keep later launcher builds on the engine selected during setup.
            preference = job.workspace/('engine-'+job.identifier)
            try:
                preference.write_text(engine+'\n')
                os.replace(preference, job.workspace/'container-engine')
            finally:
                preference.unlink(missing_ok=True)
            for number in (signal.SIGINT, signal.SIGTERM):
                previous_signals[number] = signal.signal(number, lambda *_: job.cancel.set())
            print(f'Detailed log: {job.log_path}', flush=True)
            recover_containers(job)
            inspected = inspect_source(source, job)
            print('Preparing build tools...', flush=True)
            found = subprocess.run([engine, 'image', 'inspect', image_name()], capture_output=True)
            if found.returncode:
                run_process(job, build_command(ROOT, engine), cwd=ROOT)
            result = execute_install(job, ROOT, inspected)
        launcher = ROOT/'build/native'/('kirby-launcher.exe' if WINDOWS else 'kirby-launcher')
        print(f'Installation complete. Open: {launcher}\nGame data: {result["data_root"]}')
        if (ROOT/'local/user/settings.toml').exists() or (ROOT/'local/user/launcher.cfg').exists():
            print('Your preferences were kept. Select the game data folder above in Install if needed.')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        if job is not None:
            print(f'Details: {job.log_path}', file=sys.stderr)
        return 1
    except Exception as error:
        from installer.jobs import Cancelled
        if not isinstance(error, Cancelled):
            raise
        print(str(error), file=sys.stderr)
        return 2
    finally:
        for number, handler in previous_signals.items():
            signal.signal(number, handler)


if __name__ == '__main__':
    sys.exit(main())
