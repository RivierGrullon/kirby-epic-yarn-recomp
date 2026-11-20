#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and smoke-test just the redistributable C++ frontend, without game inputs."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile

from installer.qt_frontend import validate

ROOT = Path(__file__).resolve().parents[1]


def deploy(prefix, output, compiler=None):
    prefix, output = Path(prefix).resolve(strict=True), Path(output).absolute()
    if output.exists():
        raise ValueError(f'Choose a fresh deployment directory: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    dirs = [str(prefix/'bin')]
    if compiler:
        compiler = Path(compiler).resolve(strict=True)
        dirs.insert(0, str(compiler.parent))
    environment['PATH'] = os.pathsep.join([*dirs, environment['PATH']])
    with tempfile.TemporaryDirectory(prefix='.qt-build-', dir=output.parent) as temp:
        build, stage = Path(temp)/'obj', Path(temp)/'qt-launcher'
        configure = ['cmake', '-S', ROOT/'launcher/qt', '-B', build, '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_PREFIX_PATH={prefix}']
        if compiler:
            configure.append(f'-DCMAKE_CXX_COMPILER={compiler}')
        if os.name == 'nt':
            configure.append(f'-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={stage}')
        else:
            configure += ['-DKIRBY_DEPLOY_QT=ON', f'-DCMAKE_INSTALL_PREFIX={stage}']
        subprocess.run(configure, env=environment, check=True)
        subprocess.run(['cmake', '--build', build, '--parallel', '4'], env=environment, check=True)
        if os.name != 'nt':
            subprocess.run(['cmake', '--install', build], env=environment, check=True)
        program = validate(stage)
        (stage/'README.txt').write_text(
            "Kirby's Epic Yarn - C++ Qt launcher frontend\n\n"
            "Kirby Setup installs this frontend and its Qt libraries alongside the native host.\n"
            "After installation, select Open launcher in Setup or use the installed shortcut.\n",
            encoding='utf-8')
        shutil.copy2(ROOT/'THIRD_PARTY.md', stage/'licenses'/'THIRD_PARTY.md')
        clean = {k:v for k,v in os.environ.items() if not k.startswith(('QT_', 'QML', 'LD_LIBRARY_PATH'))}
        clean['QT_QPA_PLATFORM'] = 'offscreen'
        if os.name == 'nt':
            clean['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
        subprocess.run([program, '--self-test'], env=clean, cwd=temp, check=True, timeout=90)
        stage.rename(output)
    print(f'Ready to package: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-prefix', type=Path, required=True, help='Qt desktop SDK directory')
    parser.add_argument('--compiler', type=Path, help='Compiler matching the Qt kit')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        deploy(args.qt_prefix, args.output, args.compiler)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
