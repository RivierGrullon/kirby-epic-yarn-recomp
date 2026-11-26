#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build native Kirby on Intel or Apple Silicon using an installed Mac toolchain."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from kirby import ROOT, EXPECTED_SHA1, verify_hash
from build_commands import native_command, translate_command
from installer.jobs import Job
from installer.sources import inspect_source
from macos.toolchain import macos_architecture, macos_options
from macos.package_macos import check_macho, package


def preflight(arch, qt_prefix, deployment_target):
    arch = macos_architecture(arch)
    macos_options(arch, deployment_target)
    if sys.version_info < (3, 11):
        raise ValueError('Python 3.11 or newer is required')
    missing = [name for name in ('git', 'cmake', 'ninja', 'dotnet', 'pkg-config') if not shutil.which(name)]
    if missing:
        raise ValueError('Missing build tools: '+', '.join(missing)+'. See docs/macos.md')
    subprocess.run(['xcrun', '--find', 'clang++'], check=True, capture_output=True)
    if not (qt_prefix/'lib/cmake/Qt6/Qt6Config.cmake').is_file():
        raise ValueError('--qt-prefix must point to a Qt 6 desktop SDK for this architecture')
    return arch


def build(source, *, arch, qt_prefix, deployment_target='13.0', jobs=2, make_package=False):
    arch = preflight(arch, qt_prefix, deployment_target)
    source = Path(source).expanduser().resolve(strict=True)
    if jobs < 1:
        raise ValueError('Compiler job count must be positive')
    with Job(ROOT/'local/setup', 'macos-'+uuid.uuid4().hex, lambda event: None) as job:
        data_root = None
        if source.is_file() and source.suffix.lower() == '.dol':
            dol = source
        else:
            inspected = inspect_source(source, job)
            data_root = Path(inspected['data_root'])
            dol = data_root/'sys/main.dol'
        verify_hash(dol.read_bytes(), EXPECTED_SHA1)
        commands = [
            ['tools/bootstrap.py'],
            translate_command(dol, jobs),
            native_command('macos', jobs, jobs, platform_args=(
                '--macos-arch', arch, '--macos-deployment-target', deployment_target,
                '--qt-prefix', str(qt_prefix))),
        ]
        for command in commands:
            print(f'Running {command[0]} ...', flush=True)
            subprocess.run([sys.executable, *command], cwd=ROOT, check=True)
        directory = ROOT/f'build/macos-{arch}'
        for name in ('WiiCompiled', 'kirby-launcher', 'qt-launcher/kirby-ui'):
            check_macho(directory/name, arch)
        subprocess.run([directory/'WiiCompiled', '--build-info'], check=True, cwd=ROOT)
        # Probes get a private working directory and cannot touch the normal save.
        import tempfile
        with tempfile.TemporaryDirectory(prefix='macos-probe-', dir=job.workspace/'staging') as temporary:
            options = ['--game-data', str(data_root), '--volume', '0'] if data_root else []
            subprocess.run([directory/'WiiCompiled', '--verify-bootstrap', *options],
                           check=True, cwd=temporary, timeout=30)
            if data_root:
                subprocess.run([directory/'WiiCompiled', '--probe-init', *options],
                               check=True, cwd=temporary, timeout=30)
        if make_package:
            package(ROOT, arch, qt_prefix, deployment_target=deployment_target)
    print(f'Build ready: {directory / "kirby-launcher"}')
    print(f'Select the complete DATA folder in the launcher: {data_root or "your extracted RK5E01 Rev 0 DATA folder"}')
    print('Gameplay, Metal rendering, audio and physical controllers still require testing on this Mac.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?', type=Path, default=ROOT/'disc/DATA')
    parser.add_argument('--arch', choices=('x86_64', 'arm64'))
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--deployment-target', default='13.0')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--check', action='store_true', help='Check installed tools only')
    parser.add_argument('--package', action='store_true', help='Create a local-use app bundle after building')
    args = parser.parse_args()
    try:
        if args.check:
            arch = preflight(args.arch, args.qt_prefix, args.deployment_target)
            print(f'Toolchain ready for macOS {arch}; game and GPU compatibility are not checked.')
        else:
            build(args.source, arch=args.arch, qt_prefix=args.qt_prefix.resolve(),
                  deployment_target=args.deployment_target, jobs=args.jobs, make_package=args.package)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == '__main__':
    sys.exit(main())
