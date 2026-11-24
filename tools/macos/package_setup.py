#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the macOS installer and its source snapshot, without game data."""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from macos.toolchain import macos_architecture
from package_portable import project_snapshot


def package(root, output, *, arch=None, qt_prefix=None, tool_paths=()):
    arch = macos_architecture(arch)
    root, output = Path(root).resolve(), Path(output).absolute()
    if output.suffix != '.zip' or output.exists():
        raise ValueError('Choose a new Setup ZIP destination')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Build outside Finder-synced output directories, which can attach
    # Finder metadata while the app is being signed.
    with tempfile.TemporaryDirectory(prefix='kirby-setup-') as temporary:
        stage = Path(temporary)
        subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
                        '--windowed', '--onedir', '--name', 'Kirby Setup', '--target-arch', arch,
                        '--osx-bundle-identifier', 'dev.portinglab.kirby.setup',
                        '--distpath', stage/'dist', '--workpath', stage/'work',
                        '--specpath', stage, 'tools/setup_gui.py'], cwd=root, check=True,
                       env=dict(os.environ, PYINSTALLER_CONFIG_DIR=str(stage/'cache')))
        app = stage/'dist/Kirby Setup.app'
        # Clear inherited Finder metadata before adding the read-only Git snapshot.
        subprocess.run(['xattr', '-cr', app], check=True)
        resources = app/'Contents/Resources'
        revision = project_snapshot(root, resources/'project')
        (resources/'revision').write_text(revision+'\n', encoding='ascii')
        if qt_prefix is not None:
            (resources/'local-toolchain.json').write_text(json.dumps({
                'qt_prefix': str(Path(qt_prefix).resolve()),
                'tool_paths': [str(Path(path).resolve()) for path in tool_paths],
            }), encoding='utf-8')
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', app], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', app], check=True)
        staged_zip = stage/'setup.zip'
        subprocess.run(['ditto', '-c', '-k', '--keepParent', app, staged_zip], check=True)
        unpacked = stage/'unpacked'
        subprocess.run(['ditto', '-x', '-k', staged_zip, unpacked], check=True)
        executable = unpacked/'Kirby Setup.app/Contents/MacOS/Kirby Setup'
        subprocess.run([executable, '--self-test'], cwd=unpacked,
                       env=dict(os.environ, QT_QPA_PLATFORM='offscreen'), timeout=60, check=True)
        with tempfile.TemporaryDirectory(prefix='.kirby-zip-', dir=output.parent) as publish:
            candidate = Path(publish)/output.name
            shutil.copyfile(staged_zip, candidate)
            os.link(candidate, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('x86_64', 'arm64'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--local-qt-prefix', type=Path, help='Qt SDK for a package used on this Mac only')
    parser.add_argument('--local-tool-path', type=Path, action='append', default=[])
    args = parser.parse_args()
    try:
        print(package(Path(__file__).resolve().parents[2], args.output, arch=args.arch,
                      qt_prefix=args.local_qt_prefix, tool_paths=args.local_tool_path))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'ERROR: {error}\n')


if __name__ == '__main__':
    main()
