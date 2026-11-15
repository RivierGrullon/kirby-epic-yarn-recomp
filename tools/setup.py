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
import tempfile
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


def install_launcher(job, build, identity, image):
    import tomllib
    from installer.container import run_container
    from installer.pipeline import BuildTools
    from installer.installations import file_digest
    from installer.jobs import run_process
    from installer.platforms import is_link

    tools = BuildTools(job, ROOT)
    tools.image_id = image
    run_container(job, ROOT, ['cmake', '--build', build.relative_to(ROOT).as_posix(), '--target',
                             'kirby-launcher', '--parallel', str(tools.jobs())], image_id=image)
    name = 'kirby-launcher.exe' if WINDOWS else 'kirby-launcher'
    source = build/name
    expected = file_digest(source, job.check_cancel)
    destination = ROOT/'build/native'
    for directory in (ROOT/'build', destination):
        if is_link(directory):
            raise ValueError('Launcher output directory must not be a link')
        directory.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.launcher-', dir=destination) as temporary:
        staging = Path(temporary)
        candidate = staging/name
        shutil.copyfile(source, candidate, follow_symlinks=False)
        candidate.chmod(0o700)
        if file_digest(candidate, job.check_cancel) != expected:
            raise ValueError('Launcher changed while copying')
        libraries = sorted(path for path in build.iterdir() if path.suffix.lower()=='.dll') if WINDOWS else []
        for library in libraries:
            digest = file_digest(library, job.check_cancel)
            shutil.copyfile(library, staging/library.name, follow_symlinks=False)
            if file_digest(staging/library.name) != digest:
                raise ValueError('Launcher library changed while copying')
        environment = dict(os.environ)
        if not WINDOWS and (ROOT/'local/host-libs').is_dir():
            environment['LD_LIBRARY_PATH'] = str(ROOT/'local/host-libs')+os.pathsep+environment.get('LD_LIBRARY_PATH', '')
        document = run_process(job, [candidate, '--build-info'], capture_limit=16384, timeout=10, env=environment)
        info = tomllib.loads(document.decode('utf-8'))
        if (info.get('schema_version') != 1 or info.get('program') != 'launcher'
                or not isinstance(info.get('build'), dict)
                or any(info['build'].get(key) != value for key, value in identity.items())):
            raise ValueError('Launcher build information does not match the game build')
        job.check_cancel()
        _publish_launcher(staging, destination, [library.name for library in libraries]+[name], job)
    return destination/name


def _publish_launcher(staging, destination, names, job):
    """Replace the verified launcher and DLLs, restoring earlier files on failure."""
    from installer.installations import file_digest
    from installer.platforms import is_link
    backup=staging/'previous';backup.mkdir()
    prior=set();changed=[]
    # Back up all existing products before the first replacement. Copying also
    # leaves a running Windows launcher intact if its files cannot be replaced.
    for name in names:
        target=destination/name
        if is_link(target):
            raise ValueError(f'Launcher output must not be a link: {target}')
        if target.exists():
            expected=file_digest(target,job.check_cancel)
            shutil.copyfile(target,backup/name,follow_symlinks=False)
            (backup/name).chmod(0o700 if target.suffix.lower()!='.dll' else 0o600)
            if file_digest(backup/name,job.check_cancel)!=expected:
                raise ValueError('Previous launcher changed while backing up')
            prior.add(name)
    job.check_cancel()
    try:
        for name in names:
            os.replace(staging/name,destination/name)
            changed.append(name)
    except BaseException as error:
        failures=[]
        for name in reversed(changed):
            try:
                if name in prior:
                    os.replace(backup/name,destination/name)
                else:
                    (destination/name).unlink()
            except OSError as restore_error:
                failures.append(f'{name}: {restore_error}')
        if failures:
            # Keep the last known good bytes outside the temporary directory.
            recovery=destination/('.launcher-recovery-'+uuid.uuid4().hex)
            os.rename(backup,recovery)
            raise ValueError(f'Launcher deployment failed and could not restore all files. '
                             f'Previous files are in {recovery}: '+ '; '.join(failures)) from error
        raise


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
            result = execute_install(job, ROOT, inspected,
                                     prepare_launcher=lambda build, identity, image: install_launcher(job, build, identity, image))
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
