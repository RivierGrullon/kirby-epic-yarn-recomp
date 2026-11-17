# SPDX-License-Identifier: GPL-3.0-only
"""Package an already compiled Windows game, without disc data or user files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from installer.verification import check_pe

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path('local/releases/Kirby-Windows-x64.zip')
REQUIRED = ('kirby-launcher.exe', 'WiiCompiled.exe', 'dsp_coef.bin',
            'SDL3.dll', 'webgpu_dawn.dll', 'dxcompiler.dll', 'dxil.dll', 'libc++.dll', 'libunwind.dll')
README = """Kirby's Epic Yarn - Windows x64

1. Extract the entire ZIP into a writable folder (do not run inside the ZIP).
2. On a new PC, run vc_redist.x64.exe once to install Microsoft's runtime.
3. Open kirby-launcher.exe. Under Install, choose your extracted DATA folder.
4. Open Play and press Play.

Only RK5E01 Rev 0 (USA) is supported. Game assets are not included.
To extract a WBFS, run tools/windows/build-windows.ps1 with your WBFS in the source checkout;
the command prints the DATA folder to select. main.dol alone is not enough to play.
Settings and saves are kept in local/user inside this extracted folder.
When updating, extract to a new folder and preserve your existing local/user.
No Python, compiler, Docker or source checkout is needed to run this package.
"""


def license_files(directory):
    for parent, directories, files in os.walk(directory):
        directories[:] = [name for name in directories if name not in ('.git', 'node_modules', '__pycache__')]
        for name in files:
            if name.lower().startswith(('license', 'licence', 'copying', 'notice', 'copyright', 'third-party', 'third_party')):
                path = Path(parent) / name
                if path.is_file() and not path.is_symlink():
                    yield path


def verify_package(directory):
    environment = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_RENDER_DRIVER='software',
                       SDL_AUDIO_DRIVER='dummy', PATH=str(Path(os.environ['SystemRoot']) / 'System32'))
    for executable, argument in (('WiiCompiled.exe', '--build-info'), ('kirby-launcher.exe', '--self-test')):
        result = subprocess.run([str(directory / executable), argument], cwd=directory, env=environment,
                                capture_output=True, text=True, errors='replace', timeout=60)
        if result.returncode:
            raise ValueError(f'Portable package check failed for {executable}: {result.stderr[-2000:]} '
                             'Rebuild with the current sources before packaging.')


def package(root, redist, output=None):
    root, redist = Path(root).resolve(), Path(redist).resolve(strict=True)
    build = root / 'build/native'
    output = Path(output).absolute() if output else root / OUTPUT
    files = {}
    for name in REQUIRED:
        path = build / name
        if not path.is_file():
            raise ValueError(f'Missing build output: {path}. Build the game first.')
        files[name] = path
    for name in ('kirby-launcher.exe', 'WiiCompiled.exe'):
        check_pe(files[name])
    for path in build.glob('*.dll'):
        files[path.name] = path
    if (build / 'initial_pipeline_cache.db').is_file():
        files['initial_pipeline_cache.db'] = build / 'initial_pipeline_cache.db'
    files['vc_redist.x64.exe'] = redist
    for name in ('LICENSE', 'THIRD_PARTY.md', 'dependencies.json'):
        files[name] = root / name
    # Keep upstream notices beside the compiled components. Never traverse the
    # user's disc, saves, setup sources or generated game code.
    for directory in (root / 'licenses', root / 'vendor', build / '_deps', root / '.tools/windows'):
        for path in license_files(directory):
            files['licenses/' + path.relative_to(root).as_posix()] = path
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.resolve() in {path.resolve() for path in files.values()}:
        raise ValueError('ZIP output would overwrite a package input')
    descriptor, temporary = tempfile.mkstemp(prefix='.kirby-zip-', suffix='.zip', dir=output.parent)
    os.close(descriptor)
    try:
        manifest = {}
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, path in sorted(files.items()):
                with path.open('rb') as source:
                    manifest[name] = hashlib.file_digest(source, 'sha256').hexdigest()
                archive.write(path, name)
            archive.writestr('README.txt', README)
            archive.writestr('kirby-portable.txt', 'Kirby Windows portable package v1\n')
            archive.writestr('manifest.json', json.dumps(manifest, indent=2) + '\n')
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None:
                raise ValueError('ZIP integrity check failed')
            if os.name == 'nt':
                with tempfile.TemporaryDirectory(prefix='kirby-package-check-') as extracted:
                    archive.extractall(extracted)
                    verify_package(Path(extracted))
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    print(f'ZIP ready: {output}', flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--redist', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        package(ROOT, args.redist, args.output)
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
