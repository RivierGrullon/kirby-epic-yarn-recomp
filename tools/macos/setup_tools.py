# SPDX-License-Identifier: GPL-3.0-only
"""Native toolchain adapter for the macOS Setup worker."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

from installer.jobs import run_process
from .build_macos import preflight


class MacSetupTools:
    def __init__(self, job, root):
        self.job, self.root = job, root
        prefix = os.environ.get('KIRBY_QT_PREFIX')
        if not prefix:
            brew = shutil.which('brew')
            if brew:
                result = subprocess.run([brew, '--prefix', 'qt'], capture_output=True, text=True)
                if result.returncode == 0:
                    prefix = result.stdout.strip()
        if not prefix:
            raise ValueError('Install a native Qt 6 desktop SDK and set KIRBY_QT_PREFIX, or install Qt with Homebrew')
        self.qt_prefix = Path(prefix).resolve()
        try:
            self.arch = preflight(None, self.qt_prefix, '13.0')
        except subprocess.SubprocessError as error:
            raise ValueError('Install the Xcode Command Line Tools before running Kirby Setup') from error

    def run(self, command, **options):
        return run_process(self.job, [sys.executable, *command[1:]], cwd=self.root, **options)

    def identity(self):
        identity = hashlib.sha256()
        for command in (('clang++', '--version'), ('xcrun', '--show-sdk-version'),
                        ('cmake', '--version'), ('ninja', '--version'), ('dotnet', '--version')):
            identity.update(run_process(self.job, command, capture_limit=8192))
        identity.update(self.arch.encode('ascii'))
        identity.update(str(self.qt_prefix).encode('utf-8'))
        identity.update((self.qt_prefix/'lib/cmake/Qt6/Qt6ConfigVersion.cmake').read_bytes())
        return identity.hexdigest()
