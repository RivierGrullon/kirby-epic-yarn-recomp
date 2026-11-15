#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared Docker/Podman commands, usable from Windows and Linux."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

TOOLCHAIN_IMAGE = 'localhost/kirby-native-toolchain:stage1'
WINDOWS_TOOLCHAIN_IMAGE = 'localhost/kirby-native-toolchain:windows-v1'
ROOT = Path(__file__).resolve().parents[1]


def select_engine(requested=None):
    selected = requested if requested not in (None, 'auto') else os.environ.get('KIRBY_CONTAINER_ENGINE', 'auto')
    preference = ROOT/'local/setup/container-engine'
    if selected == 'auto' and preference.exists():
        if preference.is_symlink() or preference.stat().st_size > 32:
            raise ValueError('Invalid saved container engine; select --engine docker or --engine podman')
        selected = preference.read_text().strip()
    if selected not in ('auto', 'docker', 'podman'):
        raise ValueError('Container engine must be auto, docker or podman')
    choices = ('docker', 'podman') if os.name == 'nt' else ('podman', 'docker')
    for name in choices if selected == 'auto' else (selected,):
        if shutil.which(name):
            return name
    raise ValueError('Install Docker or Podman and make its command available on PATH. '
                     'See docs/building.md. Select one with --engine docker or --engine podman.')


def image_name(target=None):
    target = target or ('windows' if os.name == 'nt' else 'linux')
    return WINDOWS_TOOLCHAIN_IMAGE if target == 'windows' else TOOLCHAIN_IMAGE


def build_command(root, engine, target=None):
    root = Path(root).resolve()
    target = target or ('windows' if os.name == 'nt' else 'linux')
    filename = 'Containerfile.windows' if target == 'windows' else 'Containerfile'
    return [engine, 'build', '-t', image_name(target), '-f', str(root/'tools'/filename), str(root/'tools')]


def run_command(root, engine, image, command, *, managed=None):
    if not command:
        raise ValueError('A container command is required')
    arguments = [engine, 'run', '--rm']
    if engine == 'podman' and os.name != 'nt':
        arguments += ['--network=host', '--userns=keep-id', '--security-opt', 'label=disable']
    elif os.name != 'nt':
        arguments += ['--user', f'{os.getuid()}:{os.getgid()}']
    if managed:
        cidfile, job, token = managed
        if (not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', job)
                or not re.fullmatch(r'[a-f0-9]{32}', token)
                or not re.fullmatch(r'(sha256:)?[a-f0-9]{64}', image)):
            raise ValueError('Invalid managed container identity')
        arguments += ['--cidfile', str(cidfile), '--name', 'kirby-setup-'+token,
                      '--label', 'io.kirby.setup-job='+job, '--label', 'io.kirby.setup-token='+token]
    arguments += ['-e', 'HOME=/tmp/kirby-build-home', '-e', 'DOTNET_CLI_TELEMETRY_OPTOUT=1',
                  '-e', 'DOTNET_NOLOGO=1', '-e', 'GIT_CONFIG_COUNT=1',
                  '-e', 'GIT_CONFIG_KEY_0=safe.directory', '-e', 'GIT_CONFIG_VALUE_0=/workspace',
                  '-v', str(Path(root).resolve())+':/workspace:rw',
                  '-w', '/workspace', image, *map(os.fspath, command)]
    return arguments


def main():
    try:
        root = Path(__file__).resolve().parents[1]
        args = sys.argv[1:]
        engine = select_engine()
        if args == ['--build']:
            return subprocess.call(build_command(root, engine))
        managed = None
        image = image_name()
        if args and args[0] == '--managed':
            if len(args) < 7 or args[5] != '--':
                raise ValueError('Usage: container.sh --managed CIDFILE JOB TOKEN IMAGE_ID -- COMMAND [ARG...]')
            managed = (args[1], args[2], args[3])
            image, args = args[4], args[6:]
        command = run_command(root, engine, image, args, managed=managed)
        exists = subprocess.run([engine, 'image', 'inspect', image], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        if exists.returncode:
            raise ValueError('Toolchain image unavailable. Start the container engine, then run '
                             'python3 tools/setup.py SOURCE (see docs/building.md).')
        return subprocess.call(command)
    except (OSError, ValueError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
