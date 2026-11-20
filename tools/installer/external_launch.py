# SPDX-License-Identifier: GPL-3.0-only
"""Stable data paths for external launchers; reuse the verified setup worker."""
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

from .installations import InstallationStore, read_json
from .platforms import WINDOWS, is_link
from .setup_layout import _atomic, prepare_destination, update_settings_data
from .setup_model import PHASES, Session, STAGES

MARKER = 'kirby-managed.json'


class NotSetUp(ValueError):
    """There is no verified build to play: setup never finished, or its files are gone."""


def publish(root, destination, data_root):
    root, destination = Path(root).resolve(), Path(destination).resolve()
    relative = root.relative_to(destination)
    if len(relative.parts) != 2 or relative.parts[0] != '.kirby':
        raise ValueError('Managed build must be inside its destination')
    settings = destination/'local/user/settings.toml'
    for path in (destination/'local', settings.parent):
        if is_link(path):
            raise ValueError('Managed user directories must not be links')
        path.mkdir(parents=True, exist_ok=True)
    if not settings.exists():
        _atomic(settings, b'[kirby]\nschema_version = 1\n')
    update_settings_data(settings, data_root)
    # Save data stays under destination/local/user across source revisions.
    # Do not copy NAND files: Windows permission metadata uses NTFS streams.
    _atomic(destination/MARKER, json.dumps(dict(version=1, project=relative.as_posix())).encode())


def selection(destination):
    destination = Path(destination).resolve()
    if not (destination/MARKER).is_file():
        raise NotSetUp('Run Kirby Setup before playing')
    value = read_json(destination/MARKER)
    relative = value.get('project') if isinstance(value, dict) else None
    if not isinstance(value, dict) or value.get('version') != 1 or not isinstance(relative, str) or not re.fullmatch(r'\.kirby/[a-f0-9]{12}', relative):
        raise NotSetUp('Invalid managed installation selection')
    root = destination/relative
    if root.resolve() != root or is_link(root.parent) or is_link(root):
        raise ValueError('Managed build must not be a link')
    store = InstallationStore(root)
    active = store.read_active()
    if not active:
        raise NotSetUp('Run Kirby Setup before playing')
    manifest = store.manifest(active['current'])
    return root, store.executable(active['current']), manifest


def play_command(destination, extra=()):
    destination = Path(destination).resolve()
    root, executable, manifest = selection(destination)
    environment = dict(os.environ)
    for key in list(environment):
        if key.startswith(('QT_', 'QML', 'PYTHON')):
            environment.pop(key, None)
    original = environment.pop('LD_LIBRARY_PATH_ORIG', '')
    if not WINDOWS:
        environment['LD_LIBRARY_PATH'] = str(root/'local/host-libs') + (os.pathsep+original if original else '')
    else:
        import ctypes
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    command = [str(executable), '--video', '--config', str(destination/'local/user/settings.toml'),
               '--game-data', manifest['data_root']]
    if environment.get('RECOMPLAUNCHER_MODE') == 'bigpicture':
        command.append('--fullscreen')
    return [*command, *extra], environment, destination


def wait_for(process):
    previous = {}
    def stop(number, _frame):
        if process.poll() is None:
            # Workers already own/clean their build processes. Forward cancellation
            # to them instead of killing a compiler and reporting a successful build.
            process.terminate()
    for number in (signal.SIGTERM, signal.SIGINT):
        previous[number] = signal.signal(number, stop)
    return previous


def restore_signals(previous):
    for number, handler in previous.items():
        signal.signal(number, handler)


def setup_fraction(session):
    # Each of the setup screen's five phases is an equal share, filled by the current stage's own
    # progress. It stays below 1 until the build is published, so a bar never looks done early.
    if session.phase is None:
        return 0.0
    within = (session.progress or 0)/100
    return min(0.99, (session.phase+within)/len(PHASES))


def prepare(bundle, destination, source):
    source = Path(source).expanduser().absolute()
    if not source.exists():
        raise ValueError('Choose an existing USA RK5E01 Rev 0 disc image or DATA folder')
    root = prepare_destination(bundle, destination)
    worker = bundle.runtime/('kirby-setup-worker.exe' if WINDOWS else 'kirby-setup-worker')
    command = [str(worker)] if worker.is_file() else [sys.executable, '-u', str(root/'tools/install.py')]
    if getattr(sys, 'frozen', False) and not worker.is_file():
        raise ValueError('The setup worker is missing; extract the complete release')
    if sys.platform == 'linux':
        command.append('--supervise')
    command += ['--workspace', str(root/'local/setup'), '--install', str(source)]
    environment = dict(os.environ, KIRBY_PROJECT_ROOT=str(root))
    session = None
    shown = 0.0
    with subprocess.Popen(command, cwd=root, env=environment, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE) as process:
        previous = wait_for(process)
        try:
            while line := process.stdout.readline(65538):
                if len(line) > 65536 or not line.endswith(b'\n'):
                    raise ValueError('Invalid setup response length')
                event = json.loads(line)
                if session is None:
                    session = Session(event.get('job_id'), root, source)
                session.feed(line)
                label = STAGES[event['stage']]
                if session.progress is not None:
                    label += ' ('+session.progress_text+')'
                # A stage's own count restarts (download, then unpack): never move the bar back.
                shown = max(shown, setup_fraction(session))
                print(f'progress {shown:.2f} {label}', flush=True)
            code = process.wait()
        except BaseException:
            process.terminate()
            process.wait()
            raise
        finally:
            restore_signals(previous)
    if session is None:
        raise ValueError('Setup worker returned no result')
    session.finish(code)
    if session.status != 'ready':
        raise ValueError(session.error or session.label)
    active = InstallationStore(root).read_active()
    if not active or active['current'] != session.result['installation_id']:
        raise ValueError('The selected installation changed during setup')
    InstallationStore(root).manifest(active['current'])
    publish(root, destination, session.result['data_root'])
    print('progress 1 Ready to play', flush=True)


def play(destination, extra=()):
    command, environment, working = play_command(destination, extra)
    logs = working/'logs'
    logs.mkdir(exist_ok=True)
    with (logs/'game.log').open('w', encoding='utf-8') as log:
        with subprocess.Popen(command, cwd=working, env=environment, stdout=log, stderr=subprocess.STDOUT) as process:
            previous = wait_for(process)
            try:
                code = process.wait()
            finally:
                restore_signals(previous)
    return code if code >= 0 else 128-code
