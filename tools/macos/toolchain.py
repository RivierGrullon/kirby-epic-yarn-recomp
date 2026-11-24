# SPDX-License-Identifier: GPL-3.0-only
"""Native Apple toolchain selection, shared by builds and packaging."""
import platform
import re
import subprocess
import sys


def macos_architecture(requested=None):
    if sys.platform != 'darwin':
        raise ValueError('macOS builds require a Mac with Xcode Command Line Tools')
    host = platform.machine().lower()
    if host not in ('x86_64', 'arm64'):
        raise ValueError(f'Unsupported Mac architecture: {host}')
    # A translated shell would silently mix Intel tools with ARM dependencies.
    translated = subprocess.run(['sysctl', '-in', 'sysctl.proc_translated'],
                                capture_output=True, text=True, check=False)
    if translated.stdout.strip() == '1':
        raise ValueError('Run Terminal and Python natively, outside Rosetta')
    if requested and requested != host:
        raise ValueError('Build each Mac architecture on a matching native host; cross-builds are not validated')
    return requested or host


def macos_options(arch, deployment_target):
    if arch not in ('x86_64', 'arm64'):
        raise ValueError('Expected x86_64 or arm64')
    if not re.fullmatch(r'[0-9]+\.[0-9]+', deployment_target) or tuple(map(int, deployment_target.split('.'))) < (13, 0):
        raise ValueError('The macOS deployment target must be 13.0 or newer')
    return [f'-DCMAKE_OSX_ARCHITECTURES={arch}',
            f'-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}',
            '-DMKW_X86_CPU_PROFILE=v2']
