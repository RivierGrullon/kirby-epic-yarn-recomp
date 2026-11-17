# SPDX-License-Identifier: GPL-3.0-only
"""Native build orchestration; tools/windows/build-windows.ps1 supplies the private toolchain."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from kirby import ROOT, EXPECTED_SHA1, verify_hash
from installer.jobs import Job
from installer.sources import inspect_source
from windows.package_windows import package


def build(source, *, jobs, translated_jobs, mingw, cppwinrt, redist):
    source = Path(source).resolve(strict=True)
    if source.suffix.lower() not in ('.dol', '.wbfs'):
        raise ValueError('Supply a WBFS or main.dol file')

    def progress(event):
        if event.get('state') != 'progress':
            print(f"[{event.get('stage')}] {event.get('state')}", flush=True)

    # Hold the existing setup lock across extraction, translation and compilation.
    with Job(ROOT / 'local/setup', 'windows-' + uuid.uuid4().hex, progress) as job:
        data_root = None
        if source.suffix.lower() == '.wbfs':
            result = inspect_source(source, job)
            data_root = Path(result['data_root'])
            dol = data_root / 'sys/main.dol'
        else:
            dol = source
        verify_hash(dol.read_bytes(), EXPECTED_SHA1)
        commands = [
            ['bootstrap.py'],
            ['kirby.py', 'translate', '--dol', str(dol), '--threads', str(jobs)],
            ['build_native.py', '--target', 'windows', '--jobs', str(jobs),
             '--translated-jobs', str(translated_jobs), '--llvm-mingw-root', str(mingw),
             '--cppwinrt-include', str(cppwinrt)],
        ]
        for command in commands:
            print(f"Running {command[0]} ...", flush=True)
            subprocess.run([sys.executable, str(ROOT / 'tools' / command[0]), *command[1:]],
                           cwd=ROOT, check=True)
        package(ROOT, redist)
    print(f"Build complete: {ROOT / 'build/native/kirby-launcher.exe'}")
    if data_root:
        print(f"Select this DATA directory in the launcher: {data_root}")
    else:
        print('DOL-only build: select a complete extracted DATA directory in the launcher to play.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--translated-jobs', type=int, default=2)
    parser.add_argument('--llvm-mingw-root', type=Path, required=True)
    parser.add_argument('--cppwinrt-include', type=Path, required=True)
    parser.add_argument('--redist', type=Path, required=True)
    args = parser.parse_args()
    try:
        if os.name != 'nt':
            raise ValueError('This entry point supports Windows only')
        if min(args.jobs, args.translated_jobs) < 1:
            raise ValueError('Job counts must be positive')
        build(args.source, jobs=args.jobs, translated_jobs=args.translated_jobs,
              mingw=args.llvm_mingw_root, cppwinrt=args.cppwinrt_include, redist=args.redist)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('Build cancelled.', file=sys.stderr)
        return 130
    return 0


if __name__ == '__main__':
    sys.exit(main())
